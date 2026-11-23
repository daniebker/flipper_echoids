package com.echoids.app;

import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

/** Scan for wild echoids (WiFi access points) and shards (Bluetooth devices). */
final class CollectScreen extends Screen {
    CollectScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Collect";
    }

    @Override
    boolean autoRefresh() {
        return true;
    }

    @Override
    View build() {
        JSONObject st = Ui.obj(Engine.state());
        JSONArray list = Ui.arr(Engine.nearby());
        boolean scanning = st.optBoolean("scanning");
        boolean demo = st.optInt("source") == 1;
        int hour = st.optInt("hour");
        LinearLayout col = Ui.column(app);

        /* Scan control */
        LinearLayout head = Ui.card(app);
        LinearLayout top = Ui.row(app);
        LinearLayout titles = Ui.column(app);
        titles.addView(Ui.title(app, scanning ? "Scanning…" : "Ready to scan"));
        String how = demo ? "Demo mode: simulated echoids" :
                            (st.optBoolean("bluetooth") ? "WiFi for echoids, Bluetooth for shards" : "WiFi only (Bluetooth off)");
        titles.addView(Ui.text(app, how, 13, Ui.MUTED));
        top.addView(titles, Ui.weight(1));
        Button toggle = scanning ? Ui.secondaryButton(app, "Stop", v -> {
            Engine.scan(false);
            app.refresh();
        }) : Ui.button(app, "Start", v -> {
            Engine.scan(true);
            app.refresh();
        });
        top.addView(toggle);
        head.addView(top);

        String error = st.optString("error");
        if(scanning && !demo && !error.isEmpty()) {
            TextView err = Ui.text(app, "⚠ " + error, 14, Ui.BAD);
            err.setPadding(0, Ui.dp(app, 10), 0, 0);
            head.addView(err);
        }

        LinearLayout stats = Ui.row(app);
        stats.setPadding(0, Ui.dp(app, 14), 0, 0);
        stats.addView(Rows.stat(app, String.valueOf(st.optInt("spotted")), "spotted"), Ui.weight(1));
        stats.addView(Rows.stat(app, String.valueOf(st.optInt("shards")), "shards found"), Ui.weight(1));
        stats.addView(Rows.stat(app, String.valueOf(st.optInt("asleep")), String.format("asleep at %02d:00", hour)), Ui.weight(1));
        head.addView(stats);
        col.addView(head);

        /* Nearby echoids */
        col.addView(Ui.heading(app, String.format("Out now (%02d:00)", hour)));
        if(list.length() == 0) {
            LinearLayout empty = Ui.card(app);
            String msg;
            if(!scanning)
                msg = "Tap Start to look for echoids around you. Wild echoids live in WiFi access points.";
            else if(st.optInt("asleep") > 0)
                msg = st.optInt("asleep") + " echoids nearby are asleep at this hour. Each one only comes out at certain " +
                      "times of day, so come back later to meet them.";
            else
                msg = "Listening… Walk around: every WiFi access point is an echoid. On Android, new access points can " +
                      "take up to 30 seconds to show up.";
            empty.addView(Ui.text(app, msg, 15, Ui.MUTED));
            col.addView(empty);
        }
        for(int i = 0; i < list.length(); i++) {
            JSONObject e = list.optJSONObject(i);
            if(e == null) continue;
            String state = e.optString("state");
            String badge = state.equals("owned") ? "Caught" : state.equals("fought") ? "Fought" : Rows.signal(e.optInt("rssi"));
            int badgeColor = state.equals("owned") ? Ui.GOOD : state.equals("fought") ? Ui.MUTED : 0xFFDDDDDD;
            final int index = e.optInt("index");
            col.addView(Rows.echoid(app, e, badge, badgeColor, false,
                v -> app.push(new DetailScreen(app, Engine.WHERE_NEARBY, index))));
        }
        return app.page(col);
    }
}
