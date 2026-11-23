package com.echoids.app;

import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;

import org.json.JSONArray;
import org.json.JSONObject;

/** Turn shards into Echo Balls, tonics, crystals and PACs. */
final class CraftScreen extends Screen {
    CraftScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Craft";
    }

    @Override
    View build() {
        JSONObject inv = Ui.obj(Engine.inventory());
        JSONObject st = Ui.obj(Engine.state());
        LinearLayout col = Ui.column(app);

        LinearLayout head = Ui.card(app);
        head.addView(Ui.bold(app, st.optInt("shardTotal") + " shards", 18));
        head.addView(Ui.text(app, "Bluetooth devices drop shards while you scan. Element names in a recipe take " +
                                      "that element's shards of any tier; tier names take that tier from any element.", 13, Ui.MUTED));
        col.addView(head);

        JSONArray recipes = inv.optJSONArray("recipes");
        for(int i = 0; recipes != null && i < recipes.length(); i++) {
            JSONObject r = recipes.optJSONObject(i);
            final int index = r.optInt("index");
            LinearLayout card = Ui.card(app);
            LinearLayout row = Ui.row(app);
            LinearLayout info = Ui.column(app);
            info.addView(Ui.bold(app, r.optString("name"), 16));
            info.addView(Ui.text(app, r.optString("recipe"), 13, Ui.MUTED));
            String have = r.optBoolean("pac") ? "You have " + r.optInt("have") + " PACs (room for " + st.optInt("pacCapacity") + ")" :
                                                "You have " + r.optInt("have");
            info.addView(Ui.text(app, have, 12, Ui.MUTED));
            row.addView(info, Ui.weight(1));
            Button b = Ui.button(app, "Craft", v -> app.act(Engine.craft(index)));
            Ui.setEnabled(b, r.optBoolean("canCraft"));
            row.addView(b);
            card.addView(row);
            col.addView(card);
        }
        return app.page(col);
    }
}
