package com.echoids.app;

import android.app.AlertDialog;
import android.graphics.Typeface;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

/** Your items and shards. */
final class BagScreen extends Screen {
    BagScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Bag";
    }

    private static String describe(JSONObject it) {
        switch(it.optString("kind")) {
        case "ball":
            int x10 = it.optInt("catchX10");
            return "Throw in battle · catch rate ×" + (x10 / 10) + "." + (x10 % 10);
        case "heal":
            return it.optInt("heal") < 0 ? "Fully restores HP" : "Restores " + it.optInt("heal") + " HP";
        case "revive":
            return "Brings a fainted echoid back with 1 HP";
        case "xp":
            return "Half a level of XP";
        default:
            return "";
        }
    }

    @Override
    View build() {
        JSONObject inv = Ui.obj(Engine.inventory());
        LinearLayout col = Ui.column(app);

        col.addView(Ui.heading(app, "Items"));
        JSONArray items = inv.optJSONArray("items");
        for(int i = 0; items != null && i < items.length(); i++) {
            JSONObject it = items.optJSONObject(i);
            String kind = it.optString("kind");
            LinearLayout card = Ui.card(app);
            LinearLayout row = Ui.row(app);
            LinearLayout info = Ui.column(app);
            info.addView(Ui.bold(app, it.optString("name"), 16));
            info.addView(Ui.text(app, describe(it), 13, Ui.MUTED));
            row.addView(info, Ui.weight(1));
            row.addView(Ui.bold(app, "×" + it.optInt("count") + "  ", 16));
            if(!kind.equals("ball")) {
                final int id = it.optInt("id");
                final String name = it.optString("name");
                Button use = Ui.secondaryButton(app, "Use", v -> pickTarget(id, name));
                Ui.setEnabled(use, it.optInt("count") > 0);
                row.addView(use);
            }
            card.addView(row);
            col.addView(card);
        }

        col.addView(Ui.heading(app, "Shards"));
        LinearLayout shards = Ui.card(app);
        JSONArray tierNames = inv.optJSONArray("tierNames");
        LinearLayout header = Ui.row(app);
        header.addView(Ui.text(app, "", 12, Ui.MUTED), Ui.weight(2));
        for(int k = 0; tierNames != null && k < tierNames.length(); k++) {
            TextView t = Ui.text(app, tierNames.optString(k), 12, Ui.MUTED);
            t.setGravity(Gravity.END);
            header.addView(t, Ui.weight(1));
        }
        shards.addView(header);
        JSONArray rows = inv.optJSONArray("shards");
        for(int i = 0; rows != null && i < rows.length(); i++) {
            JSONObject r = rows.optJSONObject(i);
            LinearLayout row = Ui.row(app);
            row.setPadding(0, Ui.dp(app, 4), 0, Ui.dp(app, 4));
            TextView name = Ui.text(app, "●  " + r.optString("type"), 15, Ui.typeColor(r.optInt("typeId")));
            name.setTypeface(Typeface.DEFAULT_BOLD);
            row.addView(name, Ui.weight(2));
            JSONArray tiers = r.optJSONArray("tiers");
            for(int k = 0; tiers != null && k < tiers.length(); k++) {
                int n = tiers.optInt(k);
                TextView t = Ui.text(app, String.valueOf(n), 15, n > 0 ? Ui.TEXT : Ui.MUTED);
                t.setGravity(Gravity.END);
                row.addView(t, Ui.weight(1));
            }
            shards.addView(row);
        }
        col.addView(shards);
        return app.page(col);
    }

    private void pickTarget(int item, String itemName) {
        JSONArray team = Ui.arr(Engine.team());
        String[] names = new String[team.length()];
        for(int i = 0; i < team.length(); i++) {
            JSONObject t = team.optJSONObject(i);
            names[i] = t.optString("name") + "  Lv " + t.optInt("level") + "  " +
                       (t.optInt("hp") <= 0 ? "fainted" : t.optInt("hp") + "/" + t.optInt("maxHp") + " HP");
        }
        new AlertDialog.Builder(app)
            .setTitle("Use " + itemName + " on…")
            .setItems(names, (d, which) -> app.act(Engine.useItem(item, which)))
            .setNegativeButton("Cancel", null)
            .show();
    }
}
