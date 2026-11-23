package com.echoids.app;

import android.app.AlertDialog;
import android.content.Intent;
import android.net.Uri;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.Switch;

import org.json.JSONObject;

/** Scanning options, your stats, and starting over. */
final class SettingsScreen extends Screen {
    SettingsScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Settings";
    }

    @Override
    View build() {
        JSONObject st = Ui.obj(Engine.state());
        LinearLayout col = Ui.column(app);

        col.addView(Ui.heading(app, "Scanning"));
        LinearLayout scan = Ui.card(app);
        scan.addView(toggle("Demo mode", "Simulated echoids and shards, no radios needed", st.optInt("source") == 1,
            on -> app.act(Engine.setDemo(on))));
        scan.addView(Ui.space(app, 1, 10));
        scan.addView(toggle("Bluetooth shards", "Scan Bluetooth devices for shards", st.optBoolean("bluetooth"),
            on -> app.act(Engine.setBluetooth(on))));
        col.addView(scan);

        col.addView(Ui.heading(app, "Your journey"));
        LinearLayout stats = Ui.card(app);
        LinearLayout row = Ui.row(app);
        row.addView(Rows.stat(app, String.valueOf(st.optInt("captures")), "caught"), Ui.weight(1));
        row.addView(Rows.stat(app, String.valueOf(st.optInt("wins")), "battles won"), Ui.weight(1));
        row.addView(Rows.stat(app, String.valueOf(st.optInt("shardsFound")), "shards found"), Ui.weight(1));
        stats.addView(row);
        col.addView(stats);

        col.addView(Ui.heading(app, "About"));
        LinearLayout about = Ui.card(app);
        about.addView(Ui.text(app, "Echoids uses the same game rules as the Flipper Zero app. The wiki explains every " +
                                       "type, move, item and rule.", 14, Ui.MUTED));
        about.addView(Ui.space(app, 1, 10));
        about.addView(Ui.secondaryButton(app, "Open the wiki", v -> {
            Intent i = new Intent(Intent.ACTION_VIEW,
                Uri.parse("https://github.com/daniebker/flipper_echoids/blob/main/docs/wiki/Home.md"));
            try {
                app.startActivity(i);
            } catch(Exception e) {
                app.toast("No browser available");
            }
        }));
        col.addView(about);

        LinearLayout danger = Ui.card(app);
        danger.addView(Ui.secondaryButton(app, "Reset game", v -> new AlertDialog.Builder(app)
            .setTitle("Start over?")
            .setMessage("This deletes your team, PAC, items and shards, and starts a new game with your starter.")
            .setPositiveButton("Reset", (d, w) -> app.act(Engine.reset()))
            .setNegativeButton("Cancel", null)
            .show()));
        col.addView(danger);
        return app.page(col);
    }

    interface OnToggle {
        void changed(boolean on);
    }

    private View toggle(String label, String hint, boolean checked, OnToggle onToggle) {
        LinearLayout row = Ui.row(app);
        LinearLayout text = Ui.column(app);
        text.addView(Ui.bold(app, label, 16));
        text.addView(Ui.text(app, hint, 13, Ui.MUTED));
        row.addView(text, Ui.weight(1));
        Switch sw = new Switch(app);
        sw.setChecked(checked);
        sw.setOnCheckedChangeListener((b, on) -> onToggle.changed(on));
        row.addView(sw);
        return row;
    }
}
