package com.echoids.app;

/**
 * The game engine (C, in app/src/main/cpp/engine): the same rules as the
 * Flipper Zero app. State and action results come back as JSON strings.
 */
final class Engine {
    static {
        System.loadLibrary("echoids");
    }

    private Engine() {}

    static final int WHERE_TEAM = 0, WHERE_PAC = 1, WHERE_NEARBY = 2;
    static final int SIGHTING_WIFI = 0, SIGHTING_BLUETOOTH = 1;

    /** Loads the save, or starts a new game. Returns true for a new game. */
    static native boolean init(String filesDir, byte[] starterId, String deviceName);

    static native void tick();
    static native void save();

    static native void scan(boolean on);
    /** 0 = no radios needed, 1 = WiFi, 2 = WiFi and Bluetooth. */
    static native int radiosWanted();
    static native void pushSighting(int type, byte[] mac, int rssi, String name);
    static native void setRadioError(String error);

    static native String state();
    static native String nearby();
    static native String team();
    static native String pac(int start, int count);
    static native String echoid(int where, int index);
    static native String inventory();
    static native String battle();
    static native String takeEvents();
    static native String shards();

    static native String battleBegin(int nearbyIndex);
    static native String battleMove(int slot);
    static native String battleSwitch(int teamIndex);
    static native String battleBall(int ball);
    static native String battleRun();
    static native String battleClose();
    static native String craft(int recipe);
    static native String useItem(int item, int teamIndex);
    static native String setLead(int teamIndex);
    static native String teamToPac(int teamIndex);
    static native String pacToTeam(int pacIndex);
    static native String swap(int pacIndex, int teamIndex);
    static native String setBluetooth(boolean on);
    static native String reset();

    /* Dungeons */
    static native String dungeon();
    static native String jackInCheck(int nearbyIndex);
    static native void linkUp(byte[] mac);
    static native void linkDown();
    static native String jackIn(int nearbyIndex);
    static native String dungeonRoom();
    static native String dungeonNext();
    static native String dungeonLeave();
}
