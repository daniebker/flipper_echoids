package com.echoids.app;

import android.app.AlertDialog;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

/** The dungeon you're jacked in to: a row of rooms with a boss at the end. */
final class DungeonScreen extends Screen {
    DungeonScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Dungeon";
    }

    @Override
    boolean onBack() {
        app.toast("Jack out to leave the dungeon");
        return true;
    }

    private static String icon(String kind) {
        switch(kind) {
        case "cache":
            return "🎁";
        case "rest":
            return "⛺";
        case "boss":
            return "👑";
        default:
            return "⚔";
        }
    }

    @Override
    View build() {
        JSONObject d = Ui.obj(Engine.dungeon());
        LinearLayout col = Ui.column(app);
        if(!d.optBoolean("active")) return app.page(col);
        JSONArray rooms = d.optJSONArray("rooms");
        int count = rooms == null ? 0 : rooms.length();
        int roomIndex = d.optInt("room");
        JSONObject room = rooms == null ? new JSONObject() : rooms.optJSONObject(roomIndex);
        String kind = room.optString("kind");
        boolean done = d.optBoolean("roomDone");
        boolean cleared = d.optBoolean("cleared");

        /* Where you are */
        LinearLayout head = Ui.card(app);
        head.addView(Ui.title(app, d.optString("name")));
        head.addView(Ui.text(app, "Jacked in to " + d.optString("ssid"), 13, Ui.MUTED));
        LinearLayout chips = Ui.row(app);
        chips.setPadding(0, Ui.dp(app, 8), 0, 0);
        chips.addView(Ui.typeChip(app, d.optInt("type"), d.optString("typeName")));
        chips.addView(Ui.space(app, 6, 1));
        chips.addView(Ui.chip(app, "Foes Lv " + d.optInt("level"), Ui.MUTED));
        chips.addView(Ui.space(app, 6, 1));
        chips.addView(Ui.chip(app, "Boss Lv " + d.optInt("bossLevel"), Ui.BAD));
        head.addView(chips);
        col.addView(head);

        /* The path through the dungeon */
        LinearLayout path = Ui.card(app);
        path.addView(Ui.text(app, "Room " + (roomIndex + 1) + " of " + count, 13, Ui.MUTED));
        LinearLayout steps = Ui.row(app);
        steps.setPadding(0, Ui.dp(app, 8), 0, 0);
        for(int i = 0; i < count; i++) {
            JSONObject r = rooms.optJSONObject(i);
            TextView t = new TextView(app);
            t.setText(r.optBoolean("done") ? "✓" : icon(r.optString("kind")));
            t.setGravity(Gravity.CENTER);
            t.setTextSize(18);
            t.setTextColor(r.optBoolean("done") ? Ui.GOOD : Ui.TEXT);
            int bg = r.optBoolean("current") ? Ui.CARD_HI : Ui.BG;
            t.setBackground(Ui.rounded(bg, Ui.dp(app, 10)));
            if(r.optBoolean("current")) t.setTypeface(Typeface.DEFAULT_BOLD);
            t.setPadding(0, Ui.dp(app, 8), 0, Ui.dp(app, 8));
            steps.addView(t, Ui.weight(1));
            if(i < count - 1) steps.addView(Ui.space(app, 4, 1));
        }
        path.addView(steps);
        col.addView(path);

        /* This room */
        LinearLayout here = Ui.card(app);
        JSONObject foe = room.optJSONObject("foe");
        String heading;
        String action;
        switch(kind) {
        case "cache":
            heading = done ? "You emptied the shard cache." : "A cache of shards glimmers here.";
            action = "Open the cache";
            break;
        case "rest":
            heading = done ? "Your team is rested." : "A quiet corner. Your team can rest here.";
            action = "Rest";
            break;
        case "boss":
            heading = cleared ? "The boss is beaten. The dungeon is yours!" : "The boss guards the heart of the dungeon.";
            action = "Face the boss";
            break;
        default:
            heading = done ? "The way is clear." : "A wild echoid blocks the way.";
            action = "Fight";
            break;
        }
        here.addView(Ui.bold(app, heading, 16));
        col.addView(here);
        if(foe != null && !done) col.addView(Rows.echoid(app, foe, kind.equals("boss") ? "Boss" : null, Ui.BAD, false, null));

        LinearLayout buttons = Ui.column(app);
        int p = Ui.dp(app, 12);
        buttons.setPadding(p, Ui.dp(app, 6), p, 0);
        if(cleared) {
            buttons.addView(Ui.button(app, "🎉 Jack out with your rewards", v -> leave(false)));
        } else if(!done) {
            buttons.addView(Ui.button(app, action, v -> {
                if(app.act(Engine.dungeonRoom()) && (kind.equals("fight") || kind.equals("boss"))) app.push(new BattleScreen(app));
            }));
        } else {
            buttons.addView(Ui.button(app, "Go deeper ▶", v -> app.act(Engine.dungeonNext())));
        }
        if(!cleared) {
            buttons.addView(Ui.space(app, 1, 8));
            buttons.addView(Ui.secondaryButton(app, "Jack out", v -> leave(true)));
        }
        col.addView(buttons);

        LinearLayout tip = Ui.card(app);
        tip.addView(Ui.text(app, "Only your team and bag come with you. Dungeon echoids can't be caught, and there's " +
                                     "no running. If your whole team faints, or the connection drops, you're ejected.",
            13, Ui.MUTED));
        col.addView(tip);
        return app.page(col);
    }

    private void leave(boolean ask) {
        if(!ask) {
            jackOut();
            return;
        }
        new AlertDialog.Builder(app)
            .setTitle("Jack out?")
            .setMessage("You'll leave the dungeon. Next time it starts from the first room.")
            .setPositiveButton("Jack out", (d, w) -> jackOut())
            .setNegativeButton("Stay", null)
            .show();
    }

    private void jackOut() {
        app.act(Engine.dungeonLeave());
    }
}
