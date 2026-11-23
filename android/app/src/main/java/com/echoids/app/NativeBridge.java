package com.echoids.app;

/** Calls into the game's native code (src/ compiled with the Flipper shim). */
final class NativeBridge {
    static {
        System.loadLibrary("echoids");
    }

    /* Flipper InputKey / InputType values. */
    static final int KEY_UP = 0, KEY_DOWN = 1, KEY_RIGHT = 2, KEY_LEFT = 3, KEY_OK = 4, KEY_BACK = 5;
    static final int TYPE_PRESS = 0, TYPE_RELEASE = 1, TYPE_SHORT = 2, TYPE_LONG = 3, TYPE_REPEAT = 4;

    static final int SCAN_WIFI = 0, SCAN_BLUETOOTH = 1;

    private NativeBridge() {}

    /** Starts the game (once per process). mac: a stable 6-byte ID for the starter echoid. */
    static native void start(String filesDir, byte[] mac, String deviceName);

    /** False once the player has quit the game from its main menu. */
    static native boolean isRunning();

    /** Copies the 128x64 screen into pixels (one byte per pixel, 1 = black). */
    static native void render(byte[] pixels);

    static native void input(int key, int type);

    /** Milliseconds of vibration the game asked for since the last call. */
    static native int takeVibration();

    /** Asks the game to save now. */
    static native void requestSave();

    /** 0 = not scanning, 1 = scan WiFi, 2 = scan WiFi and Bluetooth. */
    static native int scanWanted();

    static native void pushScan(int type, byte[] mac, int rssi, String name);

    /** Shown in Collect when scanning can't work; null clears it. */
    static native void setScanError(String error);
}
