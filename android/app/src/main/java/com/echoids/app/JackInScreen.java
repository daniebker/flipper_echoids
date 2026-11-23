package com.echoids.app;

import android.view.View;
import android.widget.LinearLayout;

import org.json.JSONObject;

/**
 * The Jack in tab, shown while the phone is on WiFi: the dungeon hidden in
 * the network the phone is connected to, and a button to enter it.
 */
final class JackInScreen extends Screen {
    JackInScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Jack in";
    }

    @Override
    boolean autoRefresh() {
        return true; /* follows the WiFi connection */
    }

    @Override
    View build() {
        LinearLayout col = Ui.column(app);
        JSONObject d = Ui.obj(Engine.dungeon());
        if(!d.optBoolean("linked")) {
            LinearLayout card = Ui.card(app);
            if(app.link.hidden()) {
                card.addView(Ui.title(app, "Which network is this?"));
                card.addView(Ui.text(app,
                    "Android only tells apps which WiFi network you're on with location access, and with location " +
                    "turned on. Allow it to jack in.", 15, Ui.MUTED));
                card.addView(Ui.space(app, 1, 12));
                card.addView(Ui.button(app, "Allow location", v -> app.requestPermissions(RadioScanner.neededPermissions(), 1)));
            } else {
                card.addView(Ui.title(app, "Not on WiFi"));
                card.addView(Ui.text(app, "Connect your phone to a WiFi network to jack in to its dungeon.", 15, Ui.MUTED));
            }
            col.addView(card);
            return app.page(col);
        }

        /* The network and its dungeon */
        LinearLayout head = Ui.card(app);
        head.addView(Ui.title(app, d.optString("name")));
        String ssid = d.optString("ssid");
        head.addView(Ui.text(app, "Hidden in " + (ssid.isEmpty() ? "your WiFi network" : ssid), 13, Ui.MUTED));
        LinearLayout chips = Ui.row(app);
        chips.setPadding(0, Ui.dp(app, 8), 0, 0);
        chips.addView(Ui.typeChip(app, d.optInt("type"), d.optString("typeName")));
        chips.addView(Ui.space(app, 6, 1));
        chips.addView(Ui.chip(app, d.optInt("roomCount") + " rooms", Ui.MUTED));
        chips.addView(Ui.space(app, 6, 1));
        chips.addView(Ui.chip(app, "Foes Lv " + d.optInt("level"), Ui.MUTED));
        head.addView(chips);
        head.addView(Ui.space(app, 1, 14));
        head.addView(Ui.button(app, "🔌 Jack in", v -> {
            if(app.act(Engine.jackIn())) app.vibrate(60);
        }));
        col.addView(head);

        col.addView(Ui.heading(app, "The boss"));
        JSONObject boss = d.optJSONObject("boss");
        if(boss != null) col.addView(Rows.echoid(app, boss, "Lv " + d.optInt("bossLevel"), Ui.BAD, false, null));

        LinearLayout rules = Ui.card(app);
        rules.addView(Ui.text(app,
            "Inside, there's no catching and no running. Beat the boss for a Prismatic shard, Great Balls and a " +
            "Revive Crystal. Leave this WiFi network and you're thrown out.", 14, Ui.MUTED));
        col.addView(rules);
        return app.page(col);
    }
}
