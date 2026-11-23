package com.echoids.app;

import android.view.Gravity;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

/** List rows shared by several screens. */
final class Rows {
    private Rows() {}

    /**
     * An echoid card: sprite, name, species and level, type and rarity chips,
     * stars, and an optional badge and HP bar.
     */
    static LinearLayout echoid(MainActivity a, JSONObject e, String badge, int badgeColor, boolean showHp, View.OnClickListener onClick) {
        LinearLayout card = Ui.card(a);
        LinearLayout row = Ui.row(a);
        card.addView(row);
        row.addView(Ui.sprite(a, e, 56));
        row.addView(Ui.space(a, 12, 1));

        LinearLayout info = Ui.column(a);
        row.addView(info, Ui.weight(1));

        LinearLayout top = Ui.row(a);
        top.addView(Ui.bold(a, e.optString("name"), 17), Ui.weight(1));
        top.addView(Ui.text(a, "Lv " + e.optInt("level"), 14, Ui.TEXT));
        info.addView(top);

        LinearLayout mid = Ui.row(a);
        TextView species = Ui.text(a, e.optString("species"), 13, Ui.MUTED);
        mid.addView(species, Ui.weight(1));
        mid.addView(Ui.starText(a, e.optInt("stars"), 14));
        info.addView(mid);

        LinearLayout chips = Ui.row(a);
        chips.setPadding(0, Ui.dp(a, 6), 0, 0);
        chips.addView(Ui.typeChip(a, e.optInt("type"), e.optString("typeName")));
        chips.addView(Ui.rarityChip(a, e.optInt("rarity"), e.optString("rarityName")));
        if(badge != null && !badge.isEmpty()) chips.addView(Ui.chip(a, badge, badgeColor));
        info.addView(chips);

        if(showHp) {
            int hp = e.optInt("hp"), max = e.optInt("maxHp");
            LinearLayout hpRow = Ui.row(a);
            hpRow.setPadding(0, Ui.dp(a, 8), 0, 0);
            hpRow.addView(Ui.bar(a, hp, max, Ui.hpColor(hp, max)), Ui.weight(1));
            TextView t = Ui.text(a, hp <= 0 ? "  Fainted" : "  " + hp + "/" + max, 12, hp <= 0 ? Ui.BAD : Ui.MUTED);
            hpRow.addView(t);
            info.addView(hpRow);
        }
        if(onClick != null) card.setOnClickListener(onClick);
        return card;
    }

    /** Signal strength as bars. */
    static String signal(int rssi) {
        int bars = rssi >= -55 ? 4 : rssi >= -67 ? 3 : rssi >= -78 ? 2 : 1;
        String full = "▂▄▆█";
        StringBuilder b = new StringBuilder();
        for(int i = 0; i < 4; i++) b.append(i < bars ? full.charAt(i) : '·');
        return b.toString();
    }

    /** A small labelled number, for counter strips. */
    static LinearLayout stat(MainActivity a, String value, String label) {
        LinearLayout col = Ui.column(a);
        col.setGravity(Gravity.CENTER_HORIZONTAL);
        TextView v = Ui.bold(a, value, 20);
        v.setGravity(Gravity.CENTER);
        col.addView(v);
        TextView l = Ui.text(a, label, 12, Ui.MUTED);
        l.setGravity(Gravity.CENTER);
        col.addView(l);
        return col;
    }
}
