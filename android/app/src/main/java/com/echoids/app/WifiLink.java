package com.echoids.app;

import android.content.Context;
import android.net.wifi.SupplicantState;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;

import java.util.Locale;
import java.util.Objects;

/**
 * Jacking in: which WiFi network the phone is already on. The game never
 * connects to a network itself; it tells the engine when the phone joins,
 * leaves or changes network. Android only reveals the network's name and
 * BSSID with location access (and location turned on).
 */
final class WifiLink {
    private final WifiManager wifi;
    private boolean onWifi;
    private String ssid; /* null while not known */
    private String bssid;

    WifiLink(Context context) {
        wifi = (WifiManager)context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
    }

    /** On a WiFi network (even if Android hides which one). */
    boolean onWifi() {
        return onWifi;
    }

    /** On WiFi, but Android won't say which network (no location access). */
    boolean hidden() {
        return onWifi && bssid == null;
    }

    String ssid() {
        return ssid == null ? "" : ssid;
    }

    /** Checks the connection; returns true if it changed. Call about once a second. */
    @SuppressWarnings("deprecation")
    boolean poll() {
        boolean nowOn = false;
        String nowSsid = null, nowBssid = null;
        try {
            WifiInfo info = wifi == null ? null : wifi.getConnectionInfo();
            if(info != null && wifi.isWifiEnabled() && info.getSupplicantState() == SupplicantState.COMPLETED) {
                nowOn = true;
                String b = info.getBSSID();
                if(b != null && !b.equals("02:00:00:00:00:00") && !b.equals("00:00:00:00:00:00"))
                    nowBssid = b.toLowerCase(Locale.ROOT);
                String s = info.getSSID();
                if(s != null && s.startsWith("\"") && s.endsWith("\"") && s.length() >= 2) s = s.substring(1, s.length() - 1);
                if(s != null && !s.equals(WifiManager.UNKNOWN_SSID) && !s.equals("<unknown ssid>")) nowSsid = s;
            }
        } catch(RuntimeException ignored) {
        }
        boolean changed = nowOn != onWifi || !Objects.equals(nowBssid, bssid) || !Objects.equals(nowSsid, ssid);
        if(!changed) return false;

        if(nowBssid == null) {
            if(bssid != null) Engine.linkDown();
        } else {
            /* A different network ends the link (and any dungeon on it). Moving
             * to another access point of the same network keeps it. */
            boolean sameNetwork = bssid != null && Objects.equals(nowSsid, ssid);
            if(bssid != null && !sameNetwork) Engine.linkDown();
            Engine.linkUp(macBytes(nowBssid), nowSsid == null ? "" : nowSsid);
        }
        onWifi = nowOn;
        ssid = nowSsid;
        bssid = nowBssid;
        return true;
    }

    private static byte[] macBytes(String mac) {
        String[] parts = mac.split(":");
        byte[] out = new byte[6];
        for(int i = 0; i < 6 && i < parts.length; i++) {
            try {
                out[i] = (byte)Integer.parseInt(parts[i], 16);
            } catch(NumberFormatException ignored) {
            }
        }
        return out;
    }
}
