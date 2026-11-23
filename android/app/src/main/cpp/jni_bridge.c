/*
 * JNI bridge between the Android UI (Java) and the game engine (C). Every call
 * takes one lock, so the UI thread and the radio callbacks never race.
 * State and action results are JSON strings.
 */
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "engine/engine.h"
#include "shim/shim.h"

#define JNI_FN(name) Java_com_echoids_app_Engine_##name

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

/* Turns an engine-owned JSON string into a Java string and frees it. */
static jstring take(JNIEnv* env, char* json) {
    jstring s = (*env)->NewStringUTF(env, json ? json : "null");
    free(json);
    return s;
}

#define LOCKED_JSON(expr)                     \
    do {                                      \
        pthread_mutex_lock(&lock);            \
        char* json_ = (expr);                 \
        pthread_mutex_unlock(&lock);          \
        return take(env, json_);              \
    } while(0)

JNIEXPORT jboolean JNICALL
    JNI_FN(init)(JNIEnv* env, jclass cls, jstring files_dir, jbyteArray mac, jstring name) {
    (void)cls;
    uint8_t m[6] = {0};
    jsize len = (*env)->GetArrayLength(env, mac);
    (*env)->GetByteArrayRegion(env, mac, 0, len < 6 ? len : 6, (jbyte*)m);
    const char* dir = (*env)->GetStringUTFChars(env, files_dir, NULL);
    const char* n = (*env)->GetStringUTFChars(env, name, NULL);
    pthread_mutex_lock(&lock);
    shim_set_data_dir(dir);
    shim_set_device(m, n);
    bool fresh = engine_init(m, n);
    pthread_mutex_unlock(&lock);
    (*env)->ReleaseStringUTFChars(env, files_dir, dir);
    (*env)->ReleaseStringUTFChars(env, name, n);
    return fresh ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL JNI_FN(tick)(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    pthread_mutex_lock(&lock);
    engine_tick();
    pthread_mutex_unlock(&lock);
}

JNIEXPORT void JNICALL JNI_FN(save)(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    pthread_mutex_lock(&lock);
    engine_save();
    pthread_mutex_unlock(&lock);
}

JNIEXPORT void JNICALL JNI_FN(scan)(JNIEnv* env, jclass cls, jboolean on) {
    (void)env;
    (void)cls;
    pthread_mutex_lock(&lock);
    engine_scan_set(on == JNI_TRUE);
    pthread_mutex_unlock(&lock);
}

JNIEXPORT jint JNICALL JNI_FN(radiosWanted)(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    pthread_mutex_lock(&lock);
    int w = engine_radios_wanted();
    pthread_mutex_unlock(&lock);
    return w;
}

JNIEXPORT void JNICALL JNI_FN(pushSighting)(
    JNIEnv* env,
    jclass cls,
    jint type,
    jbyteArray mac,
    jint rssi,
    jstring name) {
    (void)cls;
    ScanEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = type == 1 ? ScanEventBt : ScanEventAp;
    if((*env)->GetArrayLength(env, mac) < 6) return;
    (*env)->GetByteArrayRegion(env, mac, 0, 6, (jbyte*)ev.mac);
    ev.rssi = (int8_t)(rssi < -127 ? -127 : (rssi > 0 ? 0 : rssi));
    if(name) {
        const char* n = (*env)->GetStringUTFChars(env, name, NULL);
        if(n) {
            /* Printable ASCII only: names are shown in the game's lists. */
            size_t k = 0;
            for(const char* p = n; *p && k < SCAN_NAME_LEN - 1; p++) {
                unsigned char c = (unsigned char)*p;
                if(c >= 32 && c < 127) ev.name[k++] = (char)c;
            }
            ev.name[k] = '\0';
            (*env)->ReleaseStringUTFChars(env, name, n);
        }
    }
    pthread_mutex_lock(&lock);
    engine_push_sighting(&ev);
    pthread_mutex_unlock(&lock);
}

JNIEXPORT void JNICALL JNI_FN(setRadioError)(JNIEnv* env, jclass cls, jstring error) {
    (void)cls;
    const char* e = error ? (*env)->GetStringUTFChars(env, error, NULL) : NULL;
    pthread_mutex_lock(&lock);
    engine_set_radio_error(e);
    pthread_mutex_unlock(&lock);
    if(e) (*env)->ReleaseStringUTFChars(env, error, e);
}

/* ---- State ---- */

JNIEXPORT jstring JNICALL JNI_FN(state)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_state_json());
}

JNIEXPORT jstring JNICALL JNI_FN(nearby)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_nearby_json());
}

JNIEXPORT jstring JNICALL JNI_FN(team)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_team_json());
}

JNIEXPORT jstring JNICALL JNI_FN(pac)(JNIEnv* env, jclass cls, jint start, jint count) {
    (void)cls;
    LOCKED_JSON(engine_pac_json((uint32_t)start, (uint32_t)count));
}

JNIEXPORT jstring JNICALL JNI_FN(echoid)(JNIEnv* env, jclass cls, jint where, jint index) {
    (void)cls;
    LOCKED_JSON(engine_echoid_json(where, (uint32_t)index));
}

JNIEXPORT jstring JNICALL JNI_FN(inventory)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_inventory_json());
}

JNIEXPORT jstring JNICALL JNI_FN(battle)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_battle_json());
}

JNIEXPORT jstring JNICALL JNI_FN(takeEvents)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_take_events_json());
}

/* ---- Actions ---- */

JNIEXPORT jstring JNICALL JNI_FN(battleBegin)(JNIEnv* env, jclass cls, jint index) {
    (void)cls;
    LOCKED_JSON(engine_battle_begin((uint32_t)index));
}

JNIEXPORT jstring JNICALL JNI_FN(battleMove)(JNIEnv* env, jclass cls, jint slot) {
    (void)cls;
    LOCKED_JSON(engine_battle_move((uint8_t)slot));
}

JNIEXPORT jstring JNICALL JNI_FN(battleSwitch)(JNIEnv* env, jclass cls, jint index) {
    (void)cls;
    LOCKED_JSON(engine_battle_switch((uint8_t)index));
}

JNIEXPORT jstring JNICALL JNI_FN(battleBall)(JNIEnv* env, jclass cls, jint ball) {
    (void)cls;
    LOCKED_JSON(engine_battle_ball((uint8_t)ball));
}

JNIEXPORT jstring JNICALL JNI_FN(battleRun)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_battle_run());
}

JNIEXPORT jstring JNICALL JNI_FN(battleClose)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_battle_close());
}

JNIEXPORT jstring JNICALL JNI_FN(craft)(JNIEnv* env, jclass cls, jint recipe) {
    (void)cls;
    LOCKED_JSON(engine_craft((uint32_t)recipe));
}

JNIEXPORT jstring JNICALL JNI_FN(useItem)(JNIEnv* env, jclass cls, jint item, jint index) {
    (void)cls;
    LOCKED_JSON(engine_use_item((uint8_t)item, (uint8_t)index));
}

JNIEXPORT jstring JNICALL JNI_FN(setLead)(JNIEnv* env, jclass cls, jint index) {
    (void)cls;
    LOCKED_JSON(engine_set_lead((uint8_t)index));
}

JNIEXPORT jstring JNICALL JNI_FN(teamToPac)(JNIEnv* env, jclass cls, jint index) {
    (void)cls;
    LOCKED_JSON(engine_team_to_pac((uint8_t)index));
}

JNIEXPORT jstring JNICALL JNI_FN(pacToTeam)(JNIEnv* env, jclass cls, jint index) {
    (void)cls;
    LOCKED_JSON(engine_pac_to_team((uint32_t)index));
}

JNIEXPORT jstring JNICALL JNI_FN(swap)(JNIEnv* env, jclass cls, jint pac_index, jint team_index) {
    (void)cls;
    LOCKED_JSON(engine_swap((uint32_t)pac_index, (uint8_t)team_index));
}

JNIEXPORT jstring JNICALL JNI_FN(setDemo)(JNIEnv* env, jclass cls, jboolean demo) {
    (void)cls;
    LOCKED_JSON(engine_set_source(demo == JNI_TRUE));
}

JNIEXPORT jstring JNICALL JNI_FN(setBluetooth)(JNIEnv* env, jclass cls, jboolean on) {
    (void)cls;
    LOCKED_JSON(engine_set_bluetooth(on == JNI_TRUE));
}

JNIEXPORT jstring JNICALL JNI_FN(reset)(JNIEnv* env, jclass cls) {
    (void)cls;
    LOCKED_JSON(engine_reset());
}
