package com.echoids.app;

import android.app.AlertDialog;
import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/** Everything about one echoid: in your team, in the PAC, or wild nearby. */
final class DetailScreen extends Screen {
    private final int where;
    private final int index;

    DetailScreen(MainActivity app, int where, int index) {
        super(app);
        this.where = where;
        this.index = index;
    }

    @Override
    String title() {
        return where == Engine.WHERE_NEARBY ? "Wild echoid" : where == Engine.WHERE_PAC ? "In the PAC" : "Team";
    }

    @Override
    View build() {
        JSONObject e = Ui.obj(Engine.echoid(where, index));
        LinearLayout col = Ui.column(app);
        if(e.length() == 0) {
            LinearLayout gone = Ui.card(app);
            gone.addView(Ui.text(app, "This echoid isn't here any more.", 15, Ui.MUTED));
            col.addView(gone);
            return app.page(col);
        }

        /* Portrait */
        LinearLayout head = Ui.card(app);
        head.setGravity(Gravity.CENTER_HORIZONTAL);
        head.addView(Ui.sprite(app, e, 128));
        TextView name = Ui.title(app, e.optString("name"));
        name.setGravity(Gravity.CENTER);
        name.setPadding(0, Ui.dp(app, 10), 0, 0);
        head.addView(name);
        String sub = e.optString("species");
        if(!e.optString("origin").isEmpty()) sub += " · from " + e.optString("origin");
        TextView subT = Ui.text(app, sub, 14, Ui.MUTED);
        subT.setGravity(Gravity.CENTER);
        head.addView(subT);
        LinearLayout chips = Ui.row(app);
        chips.setGravity(Gravity.CENTER);
        chips.setPadding(0, Ui.dp(app, 8), 0, 0);
        chips.addView(Ui.typeChip(app, e.optInt("type"), e.optString("typeName")));
        chips.addView(Ui.rarityChip(app, e.optInt("rarity"), e.optString("rarityName")));
        if(e.optBoolean("lead")) chips.addView(Ui.chip(app, "Lead", Ui.ACCENT));
        head.addView(chips);
        JSONObject rating = e.optJSONObject("rating");
        int overall = rating != null ? rating.optInt("overall") : 1;
        TextView stars = Ui.starText(app, overall, 24);
        stars.setGravity(Gravity.CENTER);
        stars.setPadding(0, Ui.dp(app, 6), 0, 0);
        head.addView(stars);
        if(overall == 3) {
            TextView perfect = Ui.bold(app, "PERFECT — its stats can't be stronger", 13);
            perfect.setTextColor(Ui.STAR);
            head.addView(perfect);
        }
        col.addView(head);

        /* Level and HP */
        LinearLayout lv = Ui.card(app);
        int level = e.optInt("level"), maxLevel = e.optInt("maxLevel", 100);
        LinearLayout lvRow = Ui.row(app);
        lvRow.addView(Ui.bold(app, "Level " + level, 16), Ui.weight(1));
        lvRow.addView(Ui.text(app, level >= maxLevel ? "Max level" : e.optInt("xp") + " / " + e.optInt("xpNext") + " XP", 13, Ui.MUTED));
        lv.addView(lvRow);
        lv.addView(Ui.bar(app, level >= maxLevel ? 1 : e.optInt("xp"), level >= maxLevel ? 1 : e.optInt("xpNext"), 0xFF6FA8FF));
        int hp = e.optInt("hp"), maxHp = e.optInt("maxHp");
        LinearLayout hpRow = Ui.row(app);
        hpRow.setPadding(0, Ui.dp(app, 12), 0, 0);
        hpRow.addView(Ui.bold(app, "HP", 16), Ui.weight(1));
        hpRow.addView(Ui.text(app, hp <= 0 ? "Fainted" : hp + " / " + maxHp, 13, hp <= 0 ? Ui.BAD : Ui.MUTED));
        lv.addView(hpRow);
        lv.addView(Ui.bar(app, hp, maxHp, Ui.hpColor(hp, maxHp)));
        col.addView(lv);

        /* Stats with the star rating of each random roll */
        col.addView(Ui.heading(app, "Stats"));
        LinearLayout statsCard = Ui.card(app);
        String[] keys = {"maxHp", "atk", "def", "spd"};
        JSONArray rs = rating != null ? rating.optJSONArray("stats") : null;
        for(int i = 0; i < 4; i++) {
            JSONObject r = rs != null ? rs.optJSONObject(i) : null;
            LinearLayout row = Ui.row(app);
            row.setPadding(0, Ui.dp(app, 4), 0, Ui.dp(app, 4));
            String label = r != null ? r.optString("name") : "";
            row.addView(Ui.bold(app, label, 15), new LinearLayout.LayoutParams(Ui.dp(app, 56), LinearLayout.LayoutParams.WRAP_CONTENT));
            row.addView(Ui.text(app, String.valueOf(e.optInt(keys[i])), 15, Ui.TEXT), Ui.weight(1));
            if(r != null) {
                row.addView(Ui.text(app, "roll " + r.optInt("roll") + "/" + r.optInt("max") + "  ", 12, Ui.MUTED));
                row.addView(Ui.starText(app, r.optInt("stars"), 16));
            }
            statsCard.addView(row);
        }
        if(rating != null) {
            TextView total = Ui.text(app, "Star total " + rating.optInt("total") + "/" + rating.optInt("totalMax") +
                                              ". Stars rate the random part of each stat; 3 on every stat is perfect.", 12, Ui.MUTED);
            total.setPadding(0, Ui.dp(app, 6), 0, 0);
            statsCard.addView(total);
        }
        col.addView(statsCard);

        /* Moves */
        col.addView(Ui.heading(app, "Moves"));
        LinearLayout movesCard = Ui.card(app);
        JSONArray moves = e.optJSONArray("moves");
        for(int i = 0; moves != null && i < moves.length(); i++) {
            JSONObject m = moves.optJSONObject(i);
            LinearLayout row = Ui.row(app);
            row.setPadding(0, Ui.dp(app, 4), 0, Ui.dp(app, 4));
            row.addView(Ui.bold(app, m.optString("name"), 15), Ui.weight(1));
            row.addView(Ui.typeChip(app, m.optInt("type"), m.optString("typeName")));
            row.addView(Ui.text(app, "Pow " + m.optInt("power") + " · " + m.optInt("accuracy") + "%", 13, Ui.MUTED));
            movesCard.addView(row);
        }
        JSONObject next = e.optJSONObject("nextMove");
        String nextText = next != null ? "Learns " + next.optString("name") + " (" + next.optString("typeName") + ") at Lv " + next.optInt("level") :
                                         "Knows its final moves";
        TextView nextT = Ui.text(app, nextText, 13, Ui.MUTED);
        nextT.setPadding(0, Ui.dp(app, 8), 0, 0);
        movesCard.addView(nextT);
        JSONArray cant = e.optJSONArray("cantLearn");
        if(cant != null && cant.length() == 2) {
            movesCard.addView(Ui.text(app, "Can never learn " + cant.optString(0) + " or " + cant.optString(1) + " moves", 13, Ui.MUTED));
        }
        col.addView(movesCard);

        /* Where and when */
        if(where == Engine.WHERE_NEARBY) {
            col.addView(Ui.heading(app, "Out"));
            LinearLayout when = Ui.card(app);
            when.addView(Ui.text(app, "Comes out " + Ui.hours(e.optLong("activeHours")) + " every day", 14, Ui.TEXT));
            when.addView(Ui.text(app, "Signal " + Rows.signal(e.optInt("rssi")) + " (" + e.optInt("rssi") + " dBm)", 13, Ui.MUTED));
            col.addView(when);
        }

        col.addView(actions(e));
        return app.page(col);
    }

    private View actions(JSONObject e) {
        LinearLayout box = Ui.column(app);
        int p = Ui.dp(app, 12);
        box.setPadding(p, p, p, 0);
        if(where == Engine.WHERE_NEARBY) {
            String state = e.optString("state");
            if(state.equals("wild")) {
                box.addView(Ui.button(app, "Battle!", v -> {
                    if(app.act(Engine.battleBegin(index))) {
                        app.pop();
                        app.push(new BattleScreen(app));
                    }
                }));
            } else {
                box.addView(Ui.text(app, state.equals("owned") ? "You've already caught this echoid." :
                                                               "You've already fought this one. Find others, or come back another time.",
                    14, Ui.MUTED));
            }
            /* Any network you can connect to has a dungeon. */
            box.addView(Ui.space(app, 1, 8));
            box.addView(Ui.secondaryButton(app, "🔌 Jack in to " + e.optString("ssid"),
                v -> JackIn.start(app, index, e.optString("ssid"), e.optString("mac"))));
        } else if(where == Engine.WHERE_TEAM) {
            if(!e.optBoolean("lead")) box.addView(Ui.button(app, "Make lead", v -> app.act(Engine.setLead(index))));
            box.addView(Ui.space(app, 1, 8));
            box.addView(Ui.secondaryButton(app, "Use an item", v -> useItem()));
            box.addView(Ui.space(app, 1, 8));
            box.addView(Ui.secondaryButton(app, "Send to PAC", v -> {
                if(app.act(Engine.teamToPac(index))) app.pop();
            }));
        } else {
            box.addView(Ui.button(app, "Move to team", v -> moveToTeam()));
        }
        return box;
    }

    private void useItem() {
        JSONArray items = Ui.obj(Engine.inventory()).optJSONArray("items");
        List<String> labels = new ArrayList<>();
        List<Integer> ids = new ArrayList<>();
        for(int i = 0; items != null && i < items.length(); i++) {
            JSONObject it = items.optJSONObject(i);
            String kind = it.optString("kind");
            if(it.optInt("count") > 0 && (kind.equals("heal") || kind.equals("revive") || kind.equals("xp"))) {
                labels.add(it.optString("name") + "  ×" + it.optInt("count"));
                ids.add(it.optInt("id"));
            }
        }
        if(ids.isEmpty()) {
            app.toast("No usable items: craft Tonics, Revive Crystals or Echo Crystals");
            return;
        }
        new AlertDialog.Builder(app)
            .setTitle("Use an item")
            .setItems(labels.toArray(new String[0]), (d, which) -> app.act(Engine.useItem(ids.get(which), index)))
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void moveToTeam() {
        JSONObject st = Ui.obj(Engine.state());
        if(!st.optBoolean("teamFull")) {
            if(app.act(Engine.pacToTeam(index))) app.pop();
            return;
        }
        JSONArray team = Ui.arr(Engine.team());
        String[] names = new String[team.length()];
        for(int i = 0; i < team.length(); i++) {
            JSONObject t = team.optJSONObject(i);
            names[i] = t.optString("name") + "  Lv " + t.optInt("level") + (t.optBoolean("lead") ? "  (lead)" : "");
        }
        new AlertDialog.Builder(app)
            .setTitle("Team is full: swap out who?")
            .setItems(names, (d, which) -> {
                if(app.act(Engine.swap(index, which))) app.pop();
            })
            .setNegativeButton("Cancel", null)
            .show();
    }
}
