package com.echoids.app;

import android.app.AlertDialog;
import android.app.ProgressDialog;
import android.text.InputType;
import android.widget.EditText;
import android.widget.LinearLayout;

/**
 * Jacking in to a network's dungeon: checks with the game, asks for the
 * network's password if it has one, connects the phone to it, then enters.
 */
final class JackIn {
    private JackIn() {
    }

    static void start(MainActivity app, int nearbyIndex, String ssid, String bssid) {
        if(!app.act(Engine.jackInCheck(nearbyIndex))) return;
        if(ssid.isEmpty() || ssid.equals("<hidden>")) {
            app.toast("Hidden networks can't be joined");
            return;
        }
        if(!WifiLink.supported()) {
            app.toast("Jacking in needs Android 10 or newer");
            return;
        }
        String caps = RadioScanner.security(app, bssid, ssid);
        /* Unknown security (not seen in a scan lately): ask for a password anyway
         * and treat a blank one as an open network. */
        boolean unknown = caps.isEmpty();
        WifiLink.Kind kind = WifiLink.kind(caps);
        if(kind == WifiLink.Kind.UNSUPPORTED) {
            app.toast("Enterprise and WEP networks aren't supported");
            return;
        }
        if(kind == WifiLink.Kind.OPEN && !unknown) {
            connect(app, nearbyIndex, ssid, bssid, "", kind);
            return;
        }
        EditText password = new EditText(app);
        password.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        password.setHint(unknown ? "WiFi password (blank if open)" : "WiFi password");
        password.setSingleLine();
        LinearLayout box = Ui.column(app);
        int p = Ui.dp(app, 20);
        box.setPadding(p, Ui.dp(app, 8), p, 0);
        box.addView(Ui.text(app, "To enter its dungeon, connect to the network. Android will ask you to confirm.", 14, Ui.MUTED));
        box.addView(password);
        new AlertDialog.Builder(app)
            .setTitle("Jack in to " + ssid)
            .setView(box)
            .setPositiveButton("Connect", (d, w) -> {
                String pw = password.getText().toString();
                WifiLink.Kind k = unknown ? (pw.isEmpty() ? WifiLink.Kind.OPEN : WifiLink.Kind.PERSONAL) : kind;
                connect(app, nearbyIndex, ssid, bssid, pw, k);
            })
            .setNegativeButton("Cancel", null)
            .show();
    }

    @SuppressWarnings("deprecation")
    private static void connect(MainActivity app, int nearbyIndex, String ssid, String bssid, String password, WifiLink.Kind kind) {
        ProgressDialog progress = ProgressDialog.show(app, "Jacking in", "Connecting to " + ssid + "…", true, false);
        app.link.connect(ssid, bssid, password, kind, macBytes(bssid), new WifiLink.Listener() {
            @Override
            public void connected() {
                progress.dismiss();
                if(app.act(Engine.jackIn(nearbyIndex))) {
                    app.vibrate(60);
                    app.selectTab(0); /* the Dungeon tab */
                } else {
                    app.link.release();
                }
            }

            @Override
            public void failed(String why) {
                progress.dismiss();
                app.toast(why);
            }
        });
    }

    private static byte[] macBytes(String mac) {
        String[] parts = mac.split(":");
        byte[] out = new byte[6];
        for(int i = 0; i < 6 && i < parts.length; i++)
            out[i] = (byte)Integer.parseInt(parts[i], 16);
        return out;
    }
}
