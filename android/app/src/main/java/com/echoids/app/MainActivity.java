package com.echoids.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.app.AlertDialog;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.view.Gravity;
import android.view.Menu;
import android.view.MenuItem;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.ViewTreeObserver;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONObject;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayDeque;
import java.util.Deque;

/**
 * Echoids for Android: a native UI on top of the game engine, which uses the
 * same rules as the Flipper Zero app. WiFi access points are wild echoids and
 * Bluetooth devices drop shards, found with the phone's own radios.
 */
public final class MainActivity extends Activity {
    private static final long TICK_MS = 250;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Deque<Screen> stack = new ArrayDeque<>();
    private FrameLayout content;
    private LinearLayout tabBar;
    private final TextView[] tabs = new TextView[5];
    private Screen[] tabScreens;
    private Screen dungeonScreen;
    private int currentTab;
    private boolean inDungeon;
    private RadioScanner radios;
    WifiLink link;
    private Vibrator vibrator;
    private boolean visible;
    CardReader cards; /* set in onCreate: it needs the activity's context */
    /* The last card scanned, so a card still on the phone after its battle
     * doesn't start another one straight away. */
    private byte[] lastCard;
    private long cardsArmedAt;
    private long lastSecond;
    /* While the user touches a page, and briefly after (a fling), don't rebuild it. */
    private boolean touching;
    private long lastTouchUp;
    private static final long FLING_MS = 1500;

    private final Runnable tick = new Runnable() {
        @Override
        public void run() {
            Engine.tick();
            showEvents();
            long now = System.currentTimeMillis();
            if(now - lastSecond >= 1000) {
                lastSecond = now;
                radios.tick(Engine.radiosWanted());
                /* Out of the dungeon: the game no longer needs the network. */
                if(link.active() && !Ui.obj(Engine.state()).optBoolean("wantsLink")) link.release();
                Screen s = current();
                if(s.autoRefresh() && !userScrolling()) refresh();
            }
            if(visible) handler.postDelayed(this, TICK_MS);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        cards = new CardReader(this, this::onCard);
        vibrator = (Vibrator)getSystemService(Context.VIBRATOR_SERVICE);
        radios = new RadioScanner(this);
        link = new WifiLink(this);
        getWindow().setStatusBarColor(Ui.BG);
        getWindow().setNavigationBarColor(Ui.CARD);
        if(getActionBar() != null) {
            getActionBar().setElevation(0);
            getActionBar().setBackgroundDrawable(new android.graphics.drawable.ColorDrawable(Ui.BG));
        }

        boolean fresh = Engine.init(getFilesDir().getAbsolutePath(), deviceId(), Build.MODEL == null ? "Phone" : Build.MODEL);

        tabScreens = new Screen[] {
            new CollectScreen(this), new TeamScreen(this), new PacScreen(this), new CraftScreen(this), new BagScreen(this),
        };
        dungeonScreen = new DungeonScreen(this);
        LinearLayout root = Ui.column(this);
        root.setBackgroundColor(Ui.BG);
        content = new FrameLayout(this);
        root.addView(content, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        root.addView(buildTabBar());
        setContentView(root);
        selectTab(0);

        requestPermissions(RadioScanner.neededPermissions(), 1);
        if(fresh) showWelcome();
    }

    @Override
    protected void onResume() {
        super.onResume();
        visible = true;
        handler.removeCallbacks(tick);
        handler.post(tick);
        refresh();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        link.release();
    }

    @Override
    protected void onPause() {
        super.onPause();
        visible = false;
        cards.update(false);
        cardsOn = false;
        handler.removeCallbacks(tick);
        radios.stop();
        Engine.save();
    }

    /* ---- Navigation ---- */

    Screen current() {
        if(!stack.isEmpty()) return stack.peek();
        return currentTab == 0 && inDungeon ? dungeonScreen : tabScreens[currentTab];
    }

    /* In a dungeon only the dungeon, the team and the bag are open: Collect
     * becomes the Dungeon tab, and PAC and Craft are hidden. */
    private void updateTabs() {
        boolean d = Ui.obj(Engine.state()).optBoolean("inDungeon");
        if(d != inDungeon) {
            inDungeon = d;
            stack.clear();
            currentTab = 0;
        }
        if(inDungeon && (currentTab == 2 || currentTab == 3)) currentTab = 0;
        tabs[0].setText(inDungeon ? "🏰\nDungeon" : "📡\nCollect");
        tabs[2].setVisibility(inDungeon ? View.GONE : View.VISIBLE);
        tabs[3].setVisibility(inDungeon ? View.GONE : View.VISIBLE);
        for(int i = 0; i < tabs.length; i++)
            tabs[i].setTextColor(i == currentTab ? Ui.ACCENT : Ui.MUTED);
        tabBar.setVisibility(stack.isEmpty() ? View.VISIBLE : View.GONE);
    }

    void selectTab(int index) {
        stack.clear();
        currentTab = index;
        for(int i = 0; i < tabs.length; i++) {
            tabs[i].setTextColor(i == index ? Ui.ACCENT : Ui.MUTED);
        }
        tabBar.setVisibility(View.VISIBLE);
        refresh();
    }

    void push(Screen s) {
        stack.push(s);
        tabBar.setVisibility(View.GONE);
        refresh();
    }

    void pop() {
        if(!stack.isEmpty()) stack.pop();
        tabBar.setVisibility(stack.isEmpty() ? View.VISIBLE : View.GONE);
        refresh();
    }

    /** Rebuilds the current screen, keeping the scroll position. */
    void refresh() {
        updateTabs();
        Screen s = current();
        int scrollY = 0;
        View old = content.getChildCount() > 0 ? content.getChildAt(0) : null;
        if(old instanceof ScrollView && old.getTag() == s) scrollY = old.getScrollY();
        View v = s.build();
        v.setTag(s);
        content.removeAllViews();
        content.addView(v, new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        if(v instanceof ScrollView && scrollY > 0) {
            /* Restore the scroll before the first frame is drawn; posting it
             * drew one frame at the top, which flickered every refresh. */
            final int y = scrollY;
            v.getViewTreeObserver().addOnPreDrawListener(new ViewTreeObserver.OnPreDrawListener() {
                @Override
                public boolean onPreDraw() {
                    v.getViewTreeObserver().removeOnPreDrawListener(this);
                    v.scrollTo(0, y);
                    return true;
                }
            });
        }
        setTitle(s.title());
        boolean wantCards = visible && s instanceof CardScreen;
        if(wantCards && !cardsOn) cardsArmedAt = System.currentTimeMillis();
        cardsOn = wantCards;
        cards.update(wantCards);
        if(getActionBar() != null) getActionBar().setDisplayHomeAsUpEnabled(!stack.isEmpty());
        /* Keep the screen on while scanning or battling. */
        JSONObject st = Ui.obj(Engine.state());
        if(st.optBoolean("scanning") || st.optBoolean("inBattle") || st.optBoolean("inDungeon"))
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        else
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        if(current().onBack()) return;
        if(!stack.isEmpty()) {
            pop();
        } else if(currentTab != 0) {
            selectTab(0);
        } else {
            super.onBackPressed();
        }
    }

    private View buildTabBar() {
        tabBar = Ui.row(this);
        tabBar.setBackgroundColor(Ui.CARD);
        String[][] items = {{"📡", "Collect"}, {"🛡", "Team"}, {"📦", "PAC"}, {"⚒", "Craft"}, {"🎒", "Bag"}};
        for(int i = 0; i < items.length; i++) {
            final int index = i;
            TextView t = new TextView(this);
            t.setText(items[i][0] + "\n" + items[i][1]);
            t.setGravity(Gravity.CENTER);
            t.setTextSize(12);
            t.setTypeface(Typeface.DEFAULT_BOLD);
            t.setPadding(0, Ui.dp(this, 8), 0, Ui.dp(this, 8));
            t.setOnClickListener(v -> selectTab(index));
            tabs[i] = t;
            tabBar.addView(t, Ui.weight(1));
        }
        return tabBar;
    }

    /* ---- Menu: settings and help ---- */

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        menu.add(0, 1, 0, "Settings");
        menu.add(0, 2, 1, "How to play");
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch(item.getItemId()) {
        case android.R.id.home:
            onBackPressed();
            return true;
        case 1:
            if(inDungeon)
                toast("Settings are closed in a dungeon");
            else if(!(current() instanceof SettingsScreen) && !(current() instanceof BattleScreen))
                push(new SettingsScreen(this));
            return true;
        case 2:
            showHelp();
            return true;
        default:
            return super.onOptionsItemSelected(item);
        }
    }

    void showHelp() {
        new AlertDialog.Builder(this)
            .setTitle("How to play")
            .setMessage(
                "• Collect: start scanning. WiFi access points nearby are wild echoids. Each access point's " +
                "MAC address decides its species, type, stats and name.\n\n" +
                "• Echoids only come out at certain hours. Come back at another time of day to meet others.\n\n" +
                "• Bluetooth devices drop shards. Each device recharges before it drops another.\n\n" +
                "• Craft Echo Balls from shards, weaken a wild echoid in battle, then throw a ball to catch it.\n\n" +
                "• Every move has a type. Each type beats the next: Tide → Ember → Flora → Stone → Spark → " +
                "Gale → Pulse → Void → Tide.\n\n" +
                "• Your team holds 6. Others go to the PAC; craft more PACs for more room.\n\n" +
                "• Stars rate an echoid's random stats: 3 stars on every stat is perfect.\n\n" +
                "• Jack in to a WiFi network you can connect to (open an echoid on Collect) to enter that " +
                "network's dungeon. Inside, you only have your team and bag, echoids can't be caught, and a " +
                "boss waits at the end. Foes match your team's level.")
            .setPositiveButton("Got it", null)
            .show();
    }

    private void showWelcome() {
        JSONArray team = Ui.arr(Engine.team());
        JSONObject starter = team.optJSONObject(0);
        if(starter == null) return;
        LinearLayout box = Ui.column(this);
        box.setGravity(Gravity.CENTER_HORIZONTAL);
        int p = Ui.dp(this, 20);
        box.setPadding(p, p, p, 0);
        box.addView(Ui.sprite(this, starter, 112));
        TextView name = Ui.title(this, starter.optString("name"));
        name.setGravity(Gravity.CENTER);
        name.setPadding(0, Ui.dp(this, 10), 0, 0);
        box.addView(name);
        box.addView(Ui.text(this, starter.optString("species") + " · " + starter.optString("typeName") + " · " +
                                      starter.optString("rarityName"), 14, Ui.MUTED));
        TextView t = Ui.text(this, "Your phone's echo has awakened! Every echoid is born from a MAC address; this one " +
                                       "came from yours.", 14, Ui.TEXT);
        t.setPadding(0, Ui.dp(this, 12), 0, 0);
        box.addView(t);
        new AlertDialog.Builder(this).setView(box).setPositiveButton("Let's go", null).show();
    }

    /* ---- Feedback ---- */

    void toast(String msg) {
        if(msg == null || msg.isEmpty()) return;
        Toast.makeText(this, msg, Toast.LENGTH_SHORT).show();
    }

    private boolean cardsOn;

    /** A card was tapped on the card screen: battle its echoid. */
    private void onCard(byte[] uid) {
        if(!(current() instanceof CardScreen)) return;
        boolean again = lastCard != null && java.util.Arrays.equals(uid, lastCard) &&
                        System.currentTimeMillis() - cardsArmedAt < 2000;
        lastCard = uid;
        if(again) return;
        if(act(Engine.cardBattle(Engine.CARD_NFC, uid))) {
            vibrate(60);
            push(new BattleScreen(this));
        }
    }

    /** Shows an action's message and refreshes. Returns whether it succeeded. */
    boolean act(String resultJson) {
        JSONObject r = Ui.obj(resultJson);
        toast(r.optString("msg"));
        refresh();
        return r.optBoolean("ok");
    }

    private void showEvents() {
        JSONArray events = Ui.arr(Engine.takeEvents());
        for(int i = 0; i < events.length(); i++) {
            JSONObject e = events.optJSONObject(i);
            if(e == null) continue;
            /* Collecting is quiet: echoids show up on Collect and shards on
             * the Shards screen. Only being thrown out of a dungeon is news. */
            if(e.optString("kind").equals("ejected")) {
                toast(e.optString("text"));
                vibrate(120);
            }
        }
    }

    void vibrate(int ms) {
        if(vibrator == null || !vibrator.hasVibrator()) return;
        if(Build.VERSION.SDK_INT >= 26)
            vibrator.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
        else
            vibrator.vibrate(ms);
    }

    /** A stable 6-byte ID for this device, used to create the starter echoid. */
    @SuppressLint("HardwareIds")
    private byte[] deviceId() {
        String id = Settings.Secure.getString(getContentResolver(), Settings.Secure.ANDROID_ID);
        if(id == null) id = "echoids";
        byte[] mac = new byte[6];
        try {
            byte[] h = MessageDigest.getInstance("SHA-256").digest(id.getBytes(StandardCharsets.UTF_8));
            System.arraycopy(h, 0, mac, 0, 6);
        } catch(Exception e) {
            for(int i = 0; i < 6; i++) mac[i] = (byte)id.charAt(i % id.length());
        }
        return mac;
    }

    /** A scrolling page with a column for cards. */
    @SuppressLint("ClickableViewAccessibility")
    ScrollView page(LinearLayout column) {
        ScrollView sv = new ScrollView(this);
        sv.setFillViewport(true);
        sv.setBackgroundColor(Ui.BG);
        column.setPadding(0, Ui.dp(this, 6), 0, Ui.dp(this, 16));
        sv.addView(column);
        sv.setOnTouchListener((view, e) -> {
            int a = e.getActionMasked();
            if(a == MotionEvent.ACTION_DOWN) touching = true;
            else if(a == MotionEvent.ACTION_UP || a == MotionEvent.ACTION_CANCEL) {
                touching = false;
                lastTouchUp = System.currentTimeMillis();
            }
            return false; /* let the ScrollView scroll */
        });
        return sv;
    }

    private boolean userScrolling() {
        return touching || System.currentTimeMillis() - lastTouchUp < FLING_MS;
    }

    static int textColorOn(int bg) {
        return Color.luminance(bg) > 0.5f ? Color.BLACK : Color.WHITE;
    }
}
