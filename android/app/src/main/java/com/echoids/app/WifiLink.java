package com.echoids.app;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.net.wifi.WifiNetworkSpecifier;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.util.Locale;

/**
 * Jacking in: connects the phone to one WiFi network (by its name) for the
 * game, and tells the engine when the link is up or drops. Android asks the
 * player to confirm the connection. Needs Android 10 or newer.
 */
final class WifiLink {
    interface Listener {
        void connected();
        void failed(String why);
    }

    /** What kind of network this is, from its scan flags. */
    enum Kind { OPEN, PERSONAL, PERSONAL_WPA3, UNSUPPORTED }

    private static final int TIMEOUT_MS = 30000;

    private final ConnectivityManager connectivity;
    private final Handler main = new Handler(Looper.getMainLooper());
    private ConnectivityManager.NetworkCallback callback;

    WifiLink(Context context) {
        connectivity = (ConnectivityManager)context.getSystemService(Context.CONNECTIVITY_SERVICE);
    }

    static boolean supported() {
        return Build.VERSION.SDK_INT >= 29;
    }

    static Kind kind(String caps) {
        String c = caps.toUpperCase(Locale.ROOT);
        if(c.contains("EAP") || c.contains("WEP")) return Kind.UNSUPPORTED; /* enterprise and WEP */
        if(c.contains("SAE") && !c.contains("PSK")) return Kind.PERSONAL_WPA3;
        if(c.contains("PSK") || c.contains("WPA") || c.contains("RSN")) return Kind.PERSONAL;
        return Kind.OPEN;
    }

    boolean active() {
        return callback != null;
    }

    /** Connects to the network. The listener hears back on the main thread, once. */
    void connect(String ssid, String bssid, String password, Kind kind, byte[] mac, Listener listener) {
        release();
        if(!supported()) {
            listener.failed("Jacking in needs Android 10 or newer");
            return;
        }
        WifiNetworkSpecifier.Builder spec = new WifiNetworkSpecifier.Builder().setSsid(ssid);
        /* Match by name only: pinning the BSSID fails ("No devices found") when the
         * network's other access points or bands are what's in range now. */
        try {
            if(kind == Kind.PERSONAL_WPA3)
                spec.setWpa3Passphrase(password);
            else if(kind == Kind.PERSONAL)
                spec.setWpa2Passphrase(password);
        } catch(IllegalArgumentException e) {
            listener.failed("That password can't be used");
            return;
        }
        NetworkRequest request = new NetworkRequest.Builder()
                                     .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
                                     .removeCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET)
                                     .setNetworkSpecifier(spec.build())
                                     .build();
        final boolean[] answered = {false};
        callback = new ConnectivityManager.NetworkCallback() {
            @Override
            public void onAvailable(Network network) {
                main.post(() -> {
                    Engine.linkUp(mac);
                    if(!answered[0]) {
                        answered[0] = true;
                        listener.connected();
                    }
                });
            }

            @Override
            public void onUnavailable() {
                main.post(() -> {
                    callback = null;
                    if(!answered[0]) {
                        answered[0] = true;
                        listener.failed("Couldn't connect to " + ssid);
                    }
                });
            }

            @Override
            public void onLost(Network network) {
                main.post(Engine::linkDown);
            }
        };
        try {
            connectivity.requestNetwork(request, callback, TIMEOUT_MS);
        } catch(RuntimeException e) {
            callback = null;
            listener.failed("Couldn't connect: " + e.getMessage());
        }
    }

    /** Disconnects (the game no longer needs the network). */
    void release() {
        if(callback == null) return;
        try {
            connectivity.unregisterNetworkCallback(callback);
        } catch(RuntimeException ignored) {
        }
        callback = null;
        Engine.linkDown();
    }
}
