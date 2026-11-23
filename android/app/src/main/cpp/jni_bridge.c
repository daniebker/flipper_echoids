/*
 * JNI bridge: runs the game's own C code (src/echoids.c) on a native thread
 * and connects it to the Android UI (screen, buttons, vibration) and radios.
 */
#include <jni.h>
#include <pthread.h>
#include <string.h>

#include "scanner_phone.h"
#include "shim/shim.h"

#define JNI_FN(name) Java_com_echoids_app_NativeBridge_##name

static volatile int game_running;
static pthread_mutex_t start_lock = PTHREAD_MUTEX_INITIALIZER;

static void* game_thread(void* arg) {
    (void)arg;
    echoids_app(NULL);
    game_running = 0;
    return NULL;
}

JNIEXPORT void JNICALL
    JNI_FN(start)(JNIEnv* env, jclass cls, jstring files_dir, jbyteArray mac, jstring name) {
    (void)cls;
    pthread_mutex_lock(&start_lock);
    if(!game_running) {
        const char* dir = (*env)->GetStringUTFChars(env, files_dir, NULL);
        shim_set_data_dir(dir);
        (*env)->ReleaseStringUTFChars(env, files_dir, dir);

        uint8_t m[6] = {0};
        jsize len = (*env)->GetArrayLength(env, mac);
        (*env)->GetByteArrayRegion(env, mac, 0, len < 6 ? len : 6, (jbyte*)m);
        const char* n = (*env)->GetStringUTFChars(env, name, NULL);
        shim_set_device(m, n);
        (*env)->ReleaseStringUTFChars(env, name, n);

        game_running = 1;
        pthread_t t;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if(pthread_create(&t, &attr, game_thread, NULL) != 0) game_running = 0;
        pthread_attr_destroy(&attr);
    }
    pthread_mutex_unlock(&start_lock);
}

JNIEXPORT jboolean JNICALL JNI_FN(isRunning)(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return game_running ? JNI_TRUE : JNI_FALSE;
}

/* Fills pixels (128 * 64 bytes, 1 = black) with the current screen. */
JNIEXPORT void JNICALL JNI_FN(render)(JNIEnv* env, jclass cls, jbyteArray pixels) {
    (void)cls;
    static uint8_t fb[64][128];
    shim_render(fb);
    (*env)->SetByteArrayRegion(env, pixels, 0, (jsize)sizeof(fb), (const jbyte*)fb);
}

JNIEXPORT void JNICALL JNI_FN(input)(JNIEnv* env, jclass cls, jint key, jint type) {
    (void)env;
    (void)cls;
    if(key < 0 || key >= InputKeyMAX || type < 0 || type >= InputTypeMAX) return;
    shim_input((InputKey)key, (InputType)type);
}

JNIEXPORT jint JNICALL JNI_FN(takeVibration)(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return shim_take_vibration();
}

JNIEXPORT void JNICALL JNI_FN(requestSave)(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    echoids_save_request = 1;
}

JNIEXPORT jint JNICALL JNI_FN(scanWanted)(JNIEnv* env, jclass cls) {
    (void)env;
    (void)cls;
    return scanner_phone_wanted();
}

JNIEXPORT void JNICALL JNI_FN(pushScan)(
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
    jsize len = (*env)->GetArrayLength(env, mac);
    if(len < 6) return;
    (*env)->GetByteArrayRegion(env, mac, 0, 6, (jbyte*)ev.mac);
    ev.rssi = (int8_t)(rssi < -127 ? -127 : (rssi > 0 ? 0 : rssi));
    if(name) {
        const char* n = (*env)->GetStringUTFChars(env, name, NULL);
        if(n) {
            /* Keep printable ASCII only: the Flipper font has nothing else. */
            size_t k = 0;
            for(const char* p = n; *p && k < SCAN_NAME_LEN - 1; p++) {
                unsigned char c = (unsigned char)*p;
                if(c >= 32 && c < 127) ev.name[k++] = (char)c;
            }
            ev.name[k] = '\0';
            (*env)->ReleaseStringUTFChars(env, name, n);
        }
    }
    scanner_phone_push(&ev);
}

JNIEXPORT void JNICALL JNI_FN(setScanError)(JNIEnv* env, jclass cls, jstring error) {
    (void)cls;
    if(!error) {
        scanner_phone_set_error(NULL);
        return;
    }
    const char* e = (*env)->GetStringUTFChars(env, error, NULL);
    scanner_phone_set_error(e);
    (*env)->ReleaseStringUTFChars(env, error, e);
}
