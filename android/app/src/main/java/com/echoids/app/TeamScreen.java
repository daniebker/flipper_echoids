package com.echoids.app;

import android.view.View;
import android.widget.LinearLayout;

import org.json.JSONArray;
import org.json.JSONObject;

/** The echoids you carry (up to 6). The lead starts every battle. */
final class TeamScreen extends Screen {
    TeamScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        JSONObject st = Ui.obj(Engine.state());
        return "Team " + st.optInt("team") + "/" + st.optInt("teamMax");
    }

    @Override
    boolean autoRefresh() {
        return true; /* HP regenerates */
    }

    @Override
    View build() {
        JSONArray team = Ui.arr(Engine.team());
        LinearLayout col = Ui.column(app);
        col.addView(Ui.heading(app, "Your team · tap for details"));
        for(int i = 0; i < team.length(); i++) {
            JSONObject e = team.optJSONObject(i);
            final int index = e.optInt("index");
            col.addView(Rows.echoid(app, e, e.optBoolean("lead") ? "Lead" : null, Ui.ACCENT, true,
                v -> app.push(new DetailScreen(app, Engine.WHERE_TEAM, index))));
        }
        LinearLayout tip = Ui.card(app);
        tip.addView(Ui.text(app, "Team members regain HP over time unless they've fainted; fainted ones need a Revive " +
                                     "Crystal. New catches go to the PAC once your team is full.", 13, Ui.MUTED));
        col.addView(tip);
        return app.page(col);
    }
}
