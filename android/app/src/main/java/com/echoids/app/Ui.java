package com.echoids.app;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

/** Shared look and building blocks for the app's screens. */
final class Ui {
    private Ui() {}

    static final int BG = 0xFF121214;
    static final int CARD = 0xFF1E1F23;
    static final int CARD_HI = 0xFF2A2B31;
    static final int TEXT = 0xFFECECEC;
    static final int MUTED = 0xFF9A9CA3;
    static final int ACCENT = 0xFFFF8200;
    static final int GOOD = 0xFF4CD07D;
    static final int BAD = 0xFFFF5C5C;
    static final int STAR = 0xFFFFC233;

    /** One colour per type: Tide, Ember, Flora, Stone, Spark, Gale, Pulse, Void. */
    static final int[] TYPE_COLORS = {
        0xFF3D8BFF, 0xFFFF6A3D, 0xFF3CC46B, 0xFFB08A5E, 0xFFF5C83B, 0xFF4ED1E0, 0xFFB46CFF, 0xFF7A6FA8,
    };
    static final int[] RARITY_COLORS = {0xFFB0B3BA, 0xFF5AD17E, 0xFF4EA3FF, 0xFFC27BFF, 0xFFFFB12E};

    static int typeColor(int type) {
        return type >= 0 && type < TYPE_COLORS.length ? TYPE_COLORS[type] : MUTED;
    }

    static int dp(Context c, float v) {
        return (int)TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, c.getResources().getDisplayMetrics());
    }

    static GradientDrawable rounded(int color, float radiusPx) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(radiusPx);
        return d;
    }

    /* ---- Layout ---- */

    static LinearLayout column(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.VERTICAL);
        return l;
    }

    static LinearLayout row(Context c) {
        LinearLayout l = new LinearLayout(c);
        l.setOrientation(LinearLayout.HORIZONTAL);
        l.setGravity(Gravity.CENTER_VERTICAL);
        return l;
    }

    /** A rounded card with padding; add children to it. */
    static LinearLayout card(Context c) {
        LinearLayout l = column(c);
        l.setBackground(rounded(CARD, dp(c, 16)));
        int p = dp(c, 14);
        l.setPadding(p, p, p, p);
        LinearLayout.LayoutParams lp =
            new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(c, 12), dp(c, 6), dp(c, 12), dp(c, 6));
        l.setLayoutParams(lp);
        return l;
    }

    static View space(Context c, int dpW, int dpH) {
        View v = new View(c);
        v.setLayoutParams(new LinearLayout.LayoutParams(dp(c, dpW), dp(c, dpH)));
        return v;
    }

    static LinearLayout.LayoutParams weight(float w) {
        return new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, w);
    }

    /* ---- Text ---- */

    static TextView text(Context c, String s, float sp, int color) {
        TextView t = new TextView(c);
        t.setText(s);
        t.setTextSize(sp);
        t.setTextColor(color);
        return t;
    }

    static TextView title(Context c, String s) {
        TextView t = text(c, s, 20, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    static TextView heading(Context c, String s) {
        TextView t = text(c, s, 13, MUTED);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setAllCaps(true);
        t.setLetterSpacing(0.08f);
        LinearLayout.LayoutParams lp =
            new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(c, 18), dp(c, 14), dp(c, 18), dp(c, 4));
        t.setLayoutParams(lp);
        return t;
    }

    static TextView bold(Context c, String s, float sp) {
        TextView t = text(c, s, sp, TEXT);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        return t;
    }

    /** A small coloured label, like a type or rarity tag. */
    static TextView chip(Context c, String s, int color) {
        TextView t = text(c, s, 11, Color.BLACK);
        t.setTypeface(Typeface.DEFAULT_BOLD);
        t.setBackground(rounded(color, dp(c, 10)));
        t.setPadding(dp(c, 8), dp(c, 2), dp(c, 8), dp(c, 2));
        LinearLayout.LayoutParams lp =
            new LinearLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.setMargins(0, 0, dp(c, 6), 0);
        t.setLayoutParams(lp);
        return t;
    }

    static TextView typeChip(Context c, int type, String name) {
        return chip(c, name, typeColor(type));
    }

    static TextView rarityChip(Context c, int rarity, String name) {
        TextView t = chip(c, name, rarity >= 0 && rarity < RARITY_COLORS.length ? RARITY_COLORS[rarity] : MUTED);
        return t;
    }

    static String stars(int filled) {
        StringBuilder b = new StringBuilder();
        for(int i = 0; i < 3; i++) b.append(i < filled ? '★' : '☆');
        return b.toString();
    }

    static TextView starText(Context c, int filled, float sp) {
        TextView t = text(c, stars(filled), sp, STAR);
        return t;
    }

    /* ---- Buttons ---- */

    static Button button(Context c, String label, View.OnClickListener onClick) {
        Button b = new Button(c);
        b.setText(label);
        b.setAllCaps(false);
        b.setTextColor(Color.BLACK);
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setBackground(rounded(ACCENT, dp(c, 22)));
        b.setPadding(dp(c, 16), 0, dp(c, 16), 0);
        b.setOnClickListener(onClick);
        return b;
    }

    static Button secondaryButton(Context c, String label, View.OnClickListener onClick) {
        Button b = button(c, label, onClick);
        b.setTextColor(TEXT);
        GradientDrawable d = rounded(CARD_HI, dp(c, 22));
        d.setStroke(dp(c, 1), 0xFF44464E);
        b.setBackground(d);
        return b;
    }

    static void setEnabled(Button b, boolean enabled) {
        b.setEnabled(enabled);
        b.setAlpha(enabled ? 1f : 0.4f);
    }

    /* ---- Bars ---- */

    /** A thin rounded progress bar in the given colour. */
    static ProgressBar bar(Context c, int value, int max, int color) {
        ProgressBar p = new ProgressBar(c, null, android.R.attr.progressBarStyleHorizontal);
        p.setMax(Math.max(1, max));
        p.setProgress(Math.max(0, Math.min(value, max)));
        p.setProgressTintList(android.content.res.ColorStateList.valueOf(color));
        p.setProgressBackgroundTintList(android.content.res.ColorStateList.valueOf(0xFF3A3B42));
        p.setLayoutParams(new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, dp(c, 8)));
        return p;
    }

    static int hpColor(int hp, int max) {
        if(hp <= 0) return BAD;
        float f = max > 0 ? (float)hp / max : 0;
        return f > 0.5f ? GOOD : (f > 0.2f ? STAR : BAD);
    }

    /* ---- Sprites ---- */

    private static final Map<String, Bitmap> spriteCache = new HashMap<>();

    /** The echoid's 16x16 pixel creature, scaled up with crisp pixels in its type colour. */
    static ImageView sprite(Context c, JSONObject e, int sizeDp) {
        ImageView v = new ImageView(c);
        v.setImageBitmap(spriteBitmap(e));
        int px = dp(c, sizeDp);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(px, px);
        v.setLayoutParams(lp);
        v.setScaleType(ImageView.ScaleType.FIT_CENTER);
        GradientDrawable bg = rounded(0xFF26272D, dp(c, 12));
        v.setBackground(bg);
        int pad = px / 10;
        v.setPadding(pad, pad, pad, pad);
        return v;
    }

    static Bitmap spriteBitmap(JSONObject e) {
        String key = e.optString("mac") + e.optInt("type");
        Bitmap cached = spriteCache.get(key);
        if(cached != null) return cached;
        int color = typeColor(e.optInt("type"));
        JSONArray rows = e.optJSONArray("sprite");
        Bitmap small = Bitmap.createBitmap(16, 16, Bitmap.Config.ARGB_8888);
        if(rows != null) {
            for(int y = 0; y < 16 && y < rows.length(); y++) {
                int bits = rows.optInt(y);
                for(int x = 0; x < 16; x++) {
                    if((bits & (1 << (15 - x))) != 0) small.setPixel(x, y, color);
                }
            }
        }
        /* Scale without filtering so the pixels stay sharp. */
        Bitmap big = Bitmap.createScaledBitmap(small, 128, 128, false);
        spriteCache.put(key, big);
        return big;
    }

    /* ---- JSON ---- */

    static JSONObject obj(String json) {
        try {
            return json == null || json.equals("null") ? new JSONObject() : new JSONObject(json);
        } catch(JSONException e) {
            return new JSONObject();
        }
    }

    static JSONArray arr(String json) {
        try {
            return json == null || json.equals("null") ? new JSONArray() : new JSONArray(json);
        } catch(JSONException e) {
            return new JSONArray();
        }
    }

    /** "06–09, 18–21" from a 24-bit mask of active hours. */
    static String hours(long mask) {
        StringBuilder b = new StringBuilder();
        boolean wrapsStart = (mask & 1) != 0 && (mask & (1L << 23)) != 0;
        int start = 0;
        if(wrapsStart) {
            /* Start reading after a gap so a window crossing midnight stays together. */
            while(start < 24 && (mask & (1L << start)) != 0) start++;
        }
        for(int k = 0; k < 24; k++) {
            int i = (start + k) % 24;
            boolean on = (mask & (1L << i)) != 0;
            if(on) {
                int len = 0;
                while(len < 24 && (mask & (1L << ((i + len) % 24))) != 0) len++;
                if(b.length() > 0) b.append(", ");
                b.append(String.format("%02d:00–%02d:59", i, (i + len - 1) % 24));
                k += len - 1;
            }
        }
        return b.length() > 0 ? b.toString() : "never";
    }
}
