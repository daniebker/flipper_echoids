package com.echoids.app;

import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

/** Shards found while collecting, and the devices still recharging. */
final class ShardsScreen extends Screen {
    ShardsScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Shards";
    }

    @Override
    boolean autoRefresh() {
        return true; /* new devices, and the time left counts down */
    }

    @Override
    View build() {
        JSONObject sh = Ui.obj(Engine.shards());
        LinearLayout col = Ui.column(app);

        LinearLayout head = Ui.card(app);
        LinearLayout stats = Ui.row(app);
        stats.addView(Rows.stat(app, "+" + sh.optInt("collected"), "found this outing"), Ui.weight(1));
        stats.addView(Rows.stat(app, String.valueOf(sh.optInt("cooling")), "recharging"), Ui.weight(1));
        head.addView(stats);
        StringBuilder times = new StringBuilder("A device recharges after dropping its shard: ");
        JSONArray cd = sh.optJSONArray("cooldowns");
        for(int i = 0; cd != null && i < cd.length(); i++) {
            JSONObject c = cd.optJSONObject(i);
            times.append(i == 0 ? "" : ", ").append(c.optString("tier")).append(' ').append(c.optString("time"));
        }
        TextView t = Ui.text(app, times + ".", 13, Ui.MUTED);
        t.setPadding(0, Ui.dp(app, 12), 0, 0);
        head.addView(t);
        col.addView(head);

        JSONArray devices = sh.optJSONArray("devices");
        col.addView(Ui.heading(app, "Bluetooth devices this outing"));
        if(devices == null || devices.length() == 0) {
            LinearLayout empty = Ui.card(app);
            empty.addView(Ui.text(app, "No shards yet. Start scanning on Collect: Bluetooth devices nearby drop " +
                                           "shards, and each one appears here.", 15, Ui.MUTED));
            col.addView(empty);
            return app.page(col);
        }
        for(int i = 0; i < devices.length(); i++) {
            JSONObject d = devices.optJSONObject(i);
            LinearLayout card = Ui.card(app);
            LinearLayout row = Ui.row(app);
            LinearLayout info = Ui.column(app);
            TextView name = Ui.bold(app, "● " + d.optString("tierName") + " " + d.optString("type") + " shard", 16);
            name.setTextColor(Ui.typeColor(d.optInt("typeId")));
            info.addView(name);
            String from = d.optString("name");
            info.addView(Ui.text(app, from.isEmpty() ? "Unnamed device" : from, 13, Ui.MUTED));
            row.addView(info, Ui.weight(1));

            LinearLayout status = Ui.column(app);
            status.setGravity(Gravity.END);
            if(d.optBoolean("collected")) status.addView(Ui.chip(app, "Collected", Ui.GOOD));
            int left = d.optInt("left");
            TextView when = Ui.text(app, left > 0 ? d.optString("leftText") + " left" : "Ready", 13, left > 0 ? Ui.MUTED : Ui.TEXT);
            when.setGravity(Gravity.END);
            when.setPadding(0, Ui.dp(app, 4), 0, 0);
            status.addView(when);
            row.addView(status);
            card.addView(row);
            col.addView(card);
        }
        return app.page(col);
    }
}
