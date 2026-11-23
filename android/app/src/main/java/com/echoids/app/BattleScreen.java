package com.echoids.app;

import android.app.AlertDialog;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

/** A battle against a wild echoid: moves, switching, balls and running away. */
final class BattleScreen extends Screen {
    private boolean switchDialogShown;

    BattleScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Battle";
    }

    @Override
    boolean onBack() {
        JSONObject b = Ui.obj(Engine.battle());
        if(!b.optBoolean("active")) return false;
        if(b.optString("result").equals("ongoing")) {
            app.toast(b.optBoolean("canRun") ? "Finish the battle first, or Run" : "Finish the battle first");
            return true;
        }
        app.toast(Ui.obj(Engine.battleClose()).optString("msg"));
        return false;
    }

    @Override
    View build() {
        JSONObject b = Ui.obj(Engine.battle());
        LinearLayout col = Ui.column(app);
        if(!b.optBoolean("active")) {
            col.post(app::pop);
            return app.page(col);
        }
        String result = b.optString("result");
        JSONObject wild = b.optJSONObject("wild");
        JSONObject mine = b.optJSONObject("mine");

        col.addView(fighter(wild, true));
        col.addView(fighter(mine, false));

        /* Battle log */
        LinearLayout log = Ui.card(app);
        JSONArray lines = b.optJSONArray("log");
        for(int i = 0; lines != null && i < lines.length(); i++) {
            TextView t = Ui.text(app, lines.optString(i), 15, i == lines.length() - 1 ? Ui.TEXT : Ui.MUTED);
            log.addView(t);
        }
        col.addView(log);

        if(result.equals("ongoing")) {
            if(b.optBoolean("mustSwitch")) {
                LinearLayout c = Ui.card(app);
                c.addView(Ui.bold(app, "Your echoid fainted! Choose who goes next.", 16));
                c.addView(Ui.space(app, 1, 10));
                c.addView(Ui.button(app, "Choose", v -> switchDialog(true)));
                col.addView(c);
                if(!switchDialogShown) {
                    switchDialogShown = true;
                    col.post(() -> switchDialog(true));
                }
            } else {
                switchDialogShown = false;
                col.addView(Ui.heading(app, "Moves"));
                col.addView(moveGrid(b.optJSONArray("moves")));
                LinearLayout actions = Ui.row(app);
                int p = Ui.dp(app, 12);
                actions.setPadding(p, Ui.dp(app, 8), p, 0);
                Button sw = Ui.secondaryButton(app, "Switch", v -> switchDialog(false));
                actions.addView(sw, Ui.weight(1));
                /* No balls and no running in a dungeon: the engine says which apply. */
                if(b.optBoolean("canCatch")) {
                    actions.addView(Ui.space(app, 8, 1));
                    actions.addView(Ui.secondaryButton(app, "Ball", v -> ballDialog(b)), Ui.weight(1));
                }
                if(b.optBoolean("canRun")) {
                    actions.addView(Ui.space(app, 8, 1));
                    actions.addView(Ui.secondaryButton(app, "Run", v -> app.act(Engine.battleRun())), Ui.weight(1));
                }
                col.addView(actions);
                if(b.optBoolean("inDungeon")) {
                    TextView note = Ui.text(app, b.optString("catchBlocked") + " · No running", 13, Ui.MUTED);
                    note.setPadding(p, Ui.dp(app, 8), p, 0);
                    col.addView(note);
                }
            }
        } else {
            col.addView(outcome(b, result, wild));
        }
        return app.page(col);
    }

    private View fighter(JSONObject e, boolean wild) {
        LinearLayout card = Ui.card(app);
        LinearLayout row = Ui.row(app);
        card.addView(row);
        LinearLayout info = Ui.column(app);
        LinearLayout nameRow = Ui.row(app);
        nameRow.addView(Ui.bold(app, (wild ? "Wild " : "") + e.optString("name"), 18), Ui.weight(1));
        nameRow.addView(Ui.text(app, "Lv " + e.optInt("level"), 15, Ui.TEXT));
        info.addView(nameRow);
        LinearLayout chips = Ui.row(app);
        chips.setPadding(0, Ui.dp(app, 4), 0, Ui.dp(app, 6));
        chips.addView(Ui.typeChip(app, e.optInt("type"), e.optString("typeName")));
        if(wild) chips.addView(Ui.rarityChip(app, e.optInt("rarity"), e.optString("rarityName")));
        chips.addView(Ui.starText(app, e.optInt("stars"), 13));
        info.addView(chips);
        int hp = e.optInt("hp"), max = e.optInt("maxHp");
        info.addView(Ui.bar(app, hp, max, Ui.hpColor(hp, max)));
        info.addView(Ui.text(app, "HP " + hp + " / " + max, 12, Ui.MUTED));
        if(wild) {
            row.addView(info, Ui.weight(1));
            row.addView(Ui.space(app, 12, 1));
            row.addView(Ui.sprite(app, e, 88));
        } else {
            row.addView(Ui.sprite(app, e, 64));
            row.addView(Ui.space(app, 12, 1));
            row.addView(info, Ui.weight(1));
        }
        return card;
    }

    private View moveGrid(JSONArray moves) {
        LinearLayout grid = Ui.column(app);
        int p = Ui.dp(app, 12);
        grid.setPadding(p, 0, p, 0);
        LinearLayout row = null;
        for(int i = 0; moves != null && i < moves.length(); i++) {
            if(i % 2 == 0) {
                row = Ui.row(app);
                grid.addView(row);
            }
            JSONObject m = moves.optJSONObject(i);
            final int slot = i;
            row.addView(moveButton(m, v -> app.act(Engine.battleMove(slot))), Ui.weight(1));
            if(i % 2 == 0) row.addView(Ui.space(app, 8, 1));
        }
        if(moves != null && moves.length() % 2 == 1 && row != null) row.addView(new View(app), Ui.weight(1));
        return grid;
    }

    private View moveButton(JSONObject m, View.OnClickListener onClick) {
        int color = Ui.typeColor(m.optInt("type"));
        LinearLayout b = Ui.column(app);
        GradientDrawable bg = Ui.rounded(Ui.CARD, Ui.dp(app, 14));
        bg.setStroke(Ui.dp(app, 2), color);
        b.setBackground(bg);
        int p = Ui.dp(app, 10);
        b.setPadding(p, p, p, p);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1);
        lp.setMargins(0, Ui.dp(app, 4), 0, Ui.dp(app, 4));
        b.setLayoutParams(lp);
        TextView name = Ui.bold(app, m.optString("name"), 15);
        b.addView(name);
        TextView meta = Ui.text(app, m.optString("typeName") + " · Pow " + m.optInt("power") + " · " + m.optInt("accuracy") + "%", 12, color);
        b.addView(meta);
        int mult = m.optInt("mult", 100);
        if(mult != 100) {
            TextView eff = Ui.text(app, mult > 100 ? "Super effective ×2" : "Not very effective ×½", 12, mult > 100 ? Ui.GOOD : Ui.BAD);
            eff.setTypeface(Typeface.DEFAULT_BOLD);
            b.addView(eff);
        }
        b.setClickable(true);
        b.setOnClickListener(onClick);
        return b;
    }

    private View outcome(JSONObject b, String result, JSONObject wild) {
        LinearLayout c = Ui.card(app);
        String head;
        int color;
        switch(result) {
        case "caught":
            head = "Caught " + wild.optString("name") + "!";
            color = Ui.GOOD;
            break;
        case "won":
            head = "You won!";
            color = Ui.GOOD;
            break;
        case "fled":
            head = "Got away safely";
            color = Ui.MUTED;
            break;
        default:
            head = "Your team fainted";
            color = Ui.BAD;
            break;
        }
        TextView h = Ui.title(app, head);
        h.setTextColor(color);
        c.addView(h);
        JSONArray results = b.optJSONArray("results");
        for(int i = 0; results != null && i < results.length(); i++) {
            TextView t = Ui.text(app, "• " + results.optString(i), 15, Ui.TEXT);
            t.setPadding(0, Ui.dp(app, 4), 0, 0);
            c.addView(t);
        }
        if(b.optInt("xp") > 0) c.addView(Ui.text(app, b.optInt("xp") + " XP shared by everyone who fought", 13, Ui.MUTED));
        c.addView(Ui.space(app, 1, 12));
        c.addView(Ui.button(app, "Continue", v -> {
            app.pop();
            app.act(Engine.battleClose());
        }));
        return c;
    }

    private void ballDialog(JSONObject b) {
        if(!b.optBoolean("canCatch")) {
            app.toast(b.optString("catchBlocked"));
            return;
        }
        JSONArray balls = b.optJSONArray("balls");
        String[] labels = new String[balls == null ? 0 : balls.length()];
        for(int i = 0; i < labels.length; i++) {
            JSONObject x = balls.optJSONObject(i);
            labels[i] = x.optString("name") + "  ×" + x.optInt("count") + "   " + x.optInt("chance") + "% catch";
        }
        new AlertDialog.Builder(app)
            .setTitle("Throw a ball")
            .setItems(labels, (d, which) -> app.act(Engine.battleBall(balls.optJSONObject(which).optInt("id"))))
            .setNegativeButton("Cancel", null)
            .show();
    }

    private void switchDialog(boolean forced) {
        JSONArray team = Ui.arr(Engine.team());
        JSONObject b = Ui.obj(Engine.battle());
        int active = b.optJSONObject("mine") != null ? b.optJSONObject("mine").optInt("index") : -1;
        String[] names = new String[team.length()];
        for(int i = 0; i < team.length(); i++) {
            JSONObject t = team.optJSONObject(i);
            String status = i == active ? "  (fighting)" : t.optInt("hp") <= 0 ? "  (fainted)" : "  " + t.optInt("hp") + "/" + t.optInt("maxHp") + " HP";
            names[i] = t.optString("name") + "  Lv " + t.optInt("level") + status;
        }
        AlertDialog.Builder d = new AlertDialog.Builder(app)
                                    .setTitle(forced ? "Who goes next?" : "Switch to (uses your turn)")
                                    .setItems(names, (dlg, which) -> {
                                        app.act(Engine.battleSwitch(which));
                                        switchDialogShown = false;
                                    });
        if(forced)
            d.setCancelable(false);
        else
            d.setNegativeButton("Cancel", null);
        d.show();
    }
}
