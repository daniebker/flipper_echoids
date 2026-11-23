package com.echoids.app;

import android.Manifest;
import android.annotation.SuppressLint;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothManager;
import android.bluetooth.le.BluetoothLeScanner;
import android.bluetooth.le.ScanCallback;
import android.bluetooth.le.ScanResult;
import android.bluetooth.le.ScanSettings;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.location.LocationManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.SystemClock;
import android.provider.Settings;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * Scans WiFi access points and Bluetooth devices with the phone's own radios
 * while the game's Collect screen is open, and passes each sighting (MAC,
 * signal strength, name) to the game.
 */
final class RadioScanner {
    private static final long WIFI_SCAN_INTERVAL_MS = 30_000; /* Android throttles scans */
    private static final long WIFI_READ_INTERVAL_MS = 5_000;
    private static final long BT_REPEAT_MS = 15_000; /* don't flood the game */

    private final Context context;
    private final WifiManager wifi;
    private final BluetoothAdapter bluetooth;
    private final Map<String, Long> btLastSent = new HashMap<>();
    private BluetoothLeScanner bleScanner;
    private boolean bleRunning;
    private boolean receiverRegistered;
    private long lastWifiScan, lastWifiRead;
    private String lastError = "";

    private final BroadcastReceiver wifiReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent intent) {
            readWifiResults();
        }
    };

    private final ScanCallback bleCallback = new ScanCallback() {
        @Override
        public void onScanResult(int callbackType, ScanResult result) {
            onBle(result);
        }

        @Override
        public void onBatchScanResults(List<ScanResult> results) {
            for(ScanResult r : results) onBle(r);
        }
    };

    RadioScanner(Context context) {
        this.context = context.getApplicationContext();
        wifi = (WifiManager)this.context.getSystemService(Context.WIFI_SERVICE);
        BluetoothManager bm = (BluetoothManager)this.context.getSystemService(Context.BLUETOOTH_SERVICE);
        bluetooth = bm != null ? bm.getAdapter() : null;
    }

    /** Permissions the scan needs on this Android version. */
    static String[] neededPermissions() {
        List<String> p = new ArrayList<>();
        p.add(Manifest.permission.ACCESS_FINE_LOCATION);
        p.add(Manifest.permission.ACCESS_COARSE_LOCATION);
        if(Build.VERSION.SDK_INT >= 31) {
            p.add(Manifest.permission.BLUETOOTH_SCAN);
            p.add(Manifest.permission.BLUETOOTH_CONNECT);
        }
        if(Build.VERSION.SDK_INT >= 33) p.add(Manifest.permission.NEARBY_WIFI_DEVICES);
        return p.toArray(new String[0]);
    }

    private boolean granted(String permission) {
        return context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    private boolean locationOn() {
        if(Build.VERSION.SDK_INT >= 28) {
            LocationManager lm = (LocationManager)context.getSystemService(Context.LOCATION_SERVICE);
            return lm != null && lm.isLocationEnabled();
        }
        int mode = Settings.Secure.getInt(
            context.getContentResolver(), Settings.Secure.LOCATION_MODE, Settings.Secure.LOCATION_MODE_OFF);
        return mode != Settings.Secure.LOCATION_MODE_OFF;
    }

    private void setError(String error) {
        String e = error == null ? "" : error;
        if(!e.equals(lastError)) {
            lastError = e;
            NativeBridge.setScanError(error);
        }
    }

    /** Called about once a second with what the game wants (see NativeBridge.scanWanted). */
    void tick(int wanted) {
        if(wanted == 0) {
            stop();
            return;
        }
        boolean wantBt = wanted == 2;

        if(!granted(Manifest.permission.ACCESS_FINE_LOCATION)) {
            setError("Allow Location + Nearby");
            stop();
            return;
        }
        boolean wifiOk = wifi != null && (wifi.isWifiEnabled() || wifi.isScanAlwaysAvailable());
        boolean btOk = bluetooth != null && bluetooth.isEnabled() &&
                       (Build.VERSION.SDK_INT < 31 || granted(Manifest.permission.BLUETOOTH_SCAN));

        if(!locationOn())
            setError("Turn on Location");
        else if(!wifiOk)
            setError("Turn on WiFi");
        else if(wantBt && !btOk)
            setError("Turn on Bluetooth");
        else
            setError(null);

        if(wifiOk) scanWifi();
        if(wantBt && btOk)
            startBle();
        else
            stopBle();
    }

    void stop() {
        stopBle();
        if(receiverRegistered) {
            try {
                context.unregisterReceiver(wifiReceiver);
            } catch(IllegalArgumentException ignored) {
            }
            receiverRegistered = false;
        }
    }

    /* ---- WiFi ---- */

    @SuppressLint("MissingPermission")
    private void scanWifi() {
        if(!receiverRegistered) {
            IntentFilter f = new IntentFilter(WifiManager.SCAN_RESULTS_AVAILABLE_ACTION);
            if(Build.VERSION.SDK_INT >= 33)
                context.registerReceiver(wifiReceiver, f, Context.RECEIVER_EXPORTED);
            else
                context.registerReceiver(wifiReceiver, f);
            receiverRegistered = true;
        }
        long now = SystemClock.elapsedRealtime();
        if(now - lastWifiScan > WIFI_SCAN_INTERVAL_MS || lastWifiScan == 0) {
            lastWifiScan = now;
            try {
                wifi.startScan();
            } catch(SecurityException ignored) {
            }
        }
        if(now - lastWifiRead > WIFI_READ_INTERVAL_MS) readWifiResults();
    }

    @SuppressLint("MissingPermission")
    private void readWifiResults() {
        lastWifiRead = SystemClock.elapsedRealtime();
        List<android.net.wifi.ScanResult> results;
        try {
            results = wifi.getScanResults();
        } catch(SecurityException e) {
            return;
        }
        if(results == null) return;
        for(android.net.wifi.ScanResult r : results) {
            byte[] mac = parseMac(r.BSSID);
            if(mac == null) continue;
            String ssid = r.SSID;
            if(ssid != null && ssid.startsWith("\"") && ssid.endsWith("\"") && ssid.length() >= 2)
                ssid = ssid.substring(1, ssid.length() - 1);
            NativeBridge.pushScan(NativeBridge.SCAN_WIFI, mac, r.level, ssid == null ? "" : ssid);
        }
    }

    /* ---- Bluetooth LE ---- */

    @SuppressLint("MissingPermission")
    private void startBle() {
        if(bleRunning) return;
        try {
            bleScanner = bluetooth.getBluetoothLeScanner();
            if(bleScanner == null) return;
            ScanSettings settings =
                new ScanSettings.Builder().setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY).build();
            bleScanner.startScan(null, settings, bleCallback);
            bleRunning = true;
        } catch(SecurityException | IllegalStateException e) {
            bleRunning = false;
        }
    }

    @SuppressLint("MissingPermission")
    private void stopBle() {
        if(!bleRunning) return;
        try {
            if(bleScanner != null) bleScanner.stopScan(bleCallback);
        } catch(SecurityException | IllegalStateException ignored) {
        }
        bleRunning = false;
    }

    @SuppressLint("MissingPermission")
    private void onBle(ScanResult result) {
        if(result == null || result.getDevice() == null) return;
        String address = result.getDevice().getAddress();
        long now = SystemClock.elapsedRealtime();
        Long last = btLastSent.get(address);
        if(last != null && now - last < BT_REPEAT_MS) return;
        btLastSent.put(address, now);
        byte[] mac = parseMac(address);
        if(mac == null) return;
        String name = null;
        try {
            name = result.getDevice().getName();
        } catch(SecurityException ignored) {
        }
        if(name == null && result.getScanRecord() != null) name = result.getScanRecord().getDeviceName();
        NativeBridge.pushScan(NativeBridge.SCAN_BLUETOOTH, mac, result.getRssi(), name == null ? "" : name);
    }

    /** "aa:bb:cc:dd:ee:ff" -> 6 bytes (null if malformed or the all-zero "unknown" MAC). */
    static byte[] parseMac(String s) {
        if(s == null) return null;
        String[] parts = s.split("[:-]");
        if(parts.length != 6) return null;
        byte[] mac = new byte[6];
        boolean any = false;
        try {
            for(int i = 0; i < 6; i++) {
                int v = Integer.parseInt(parts[i], 16);
                mac[i] = (byte)v;
                if(v != 0) any = true;
            }
        } catch(NumberFormatException e) {
            return null;
        }
        return any ? mac : null;
    }
}
