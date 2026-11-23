package com.echoids.app;

import android.util.SparseArray;
import android.view.View;
import android.view.ViewGroup;
import android.widget.BaseAdapter;
import android.widget.LinearLayout;
import android.widget.ListView;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONObject;

/** Cold PAC Storage: every echoid not in your team, loaded a page at a time. */
final class PacScreen extends Screen {
    private static final int PAGE = 20;

    PacScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        JSONObject st = Ui.obj(Engine.state());
        return "PAC " + st.optInt("pac") + "/" + st.optInt("pacCapacity");
    }

    @Override
    View build() {
        JSONObject st = Ui.obj(Engine.state());
        final int count = st.optInt("pac");
        if(count == 0) {
            LinearLayout col = Ui.column(app);
            LinearLayout empty = Ui.card(app);
            empty.addView(Ui.bold(app, "Cold PAC Storage is empty", 16));
            empty.addView(Ui.text(app, "When your team of " + st.optInt("teamMax") + " is full, new catches are stored " +
                                           "here. Each PAC holds 10 echoids; craft more PACs for more room. You have " +
                                           st.optInt("pacs") + " (room for " + st.optInt("pacCapacity") + ").", 14, Ui.MUTED));
            col.addView(empty);
            return app.page(col);
        }

        ListView list = new ListView(app);
        list.setDivider(null);
        list.setBackgroundColor(Ui.BG);
        list.setSelector(android.R.color.transparent);
        final SparseArray<JSONObject> cache = new SparseArray<>();
        list.setAdapter(new BaseAdapter() {
            @Override
            public int getCount() {
                return count;
            }

            @Override
            public Object getItem(int position) {
                JSONObject e = cache.get(position);
                if(e == null) {
                    /* Load the page around this position from the PAC file. */
                    int start = (position / PAGE) * PAGE;
                    JSONArray page = Ui.arr(Engine.pac(start, PAGE));
                    for(int i = 0; i < page.length(); i++) cache.put(start + i, page.optJSONObject(i));
                    e = cache.get(position);
                }
                return e;
            }

            @Override
            public long getItemId(int position) {
                return position;
            }

            @Override
            public View getView(int position, View convertView, ViewGroup parent) {
                JSONObject e = (JSONObject)getItem(position);
                if(e == null) return new TextView(app);
                /* The card keeps its margins inside this wrapper row. */
                LinearLayout wrap = new LinearLayout(app);
                wrap.addView(Rows.echoid(app, e, null, 0, false, null));
                return wrap;
            }
        });
        list.setOnItemClickListener((parent, view, position, id) -> app.push(new DetailScreen(app, Engine.WHERE_PAC, position)));
        return list;
    }
}
