package com.echoids.app;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;

/**
 * Echoids for Android: the Flipper Zero game's own code, shown on a scaled-up
 * 128x64 screen with a Flipper-style D-pad, using the phone's WiFi and
 * Bluetooth radios to find echoids and shards.
 */
public final class MainActivity extends Activity {
    private static final long FRAME_MS = 33; /* ~30 fps */
    private static final long LONG_PRESS_MS = 400, REPEAT_MS = 120;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final byte[] pixels = new byte[ScreenView.W * ScreenView.H];
    private ScreenView screen;
    private RadioScanner radios;
    private Vibrator vibrator;
    private boolean visible;
    private long lastRadioTick;

    private final Runnable frame = new Runnable() {
        @Override
        public void run() {
            if(!NativeBridge.isRunning()) {
                finish(); /* the player quit from the game's main menu */
                return;
            }
            NativeBridge.render(pixels);
            screen.setPixels(pixels);
            int buzz = NativeBridge.takeVibration();
            if(buzz > 0) vibrate(buzz);
            long now = System.currentTimeMillis();
            if(now - lastRadioTick > 1000) {
                lastRadioTick = now;
                radios.tick(NativeBridge.scanWanted());
            }
            if(visible) handler.postDelayed(this, FRAME_MS);
        }
    };

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        vibrator = (Vibrator)getSystemService(Context.VIBRATOR_SERVICE);
        radios = new RadioScanner(this);

        screen = new ScreenView(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.rgb(20, 20, 20));
        root.addView(screen, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(buildControls(), new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));
        setContentView(root);

        NativeBridge.start(getFilesDir().getAbsolutePath(), deviceId(), Build.MODEL == null ? "Phone" : Build.MODEL);
        requestPermissions(RadioScanner.neededPermissions(), 1);
    }

    @Override
    protected void onResume() {
        super.onResume();
        visible = true;
        handler.removeCallbacks(frame);
        handler.post(frame);
    }

    @Override
    protected void onPause() {
        super.onPause();
        visible = false;
        handler.removeCallbacks(frame);
        radios.stop();
        NativeBridge.requestSave(); /* the game saves on its next tick */
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

    private void vibrate(int ms) {
        if(vibrator == null || !vibrator.hasVibrator()) return;
        if(Build.VERSION.SDK_INT >= 26)
            vibrator.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
        else
            vibrator.vibrate(ms);
    }

    /* ---- Controls: D-pad with OK in the middle, Back on the right ---- */

    private View buildControls() {
        int unit = (int)(getResources().getDisplayMetrics().density * 64);
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);

        LinearLayout pad = new LinearLayout(this);
        pad.setOrientation(LinearLayout.VERTICAL);
        pad.setGravity(Gravity.CENTER);
        pad.addView(padRow(unit, null, button("▲", NativeBridge.KEY_UP, unit), null));
        pad.addView(padRow(unit, button("◀", NativeBridge.KEY_LEFT, unit), button("OK", NativeBridge.KEY_OK, unit),
            button("▶", NativeBridge.KEY_RIGHT, unit)));
        pad.addView(padRow(unit, null, button("▼", NativeBridge.KEY_DOWN, unit), null));
        row.addView(pad);

        FrameLayout gap = new FrameLayout(this);
        row.addView(gap, new LinearLayout.LayoutParams(unit / 2, 1));
        View back = button("↩", NativeBridge.KEY_BACK, unit);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(unit, unit);
        lp.topMargin = unit;
        row.addView(back, lp);
        return row;
    }

    private View padRow(int unit, View a, View b, View c) {
        LinearLayout r = new LinearLayout(this);
        r.setOrientation(LinearLayout.HORIZONTAL);
        View[] cells = {a, b, c};
        for(View v : cells) {
            View cell = v != null ? v : new View(this);
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(unit, unit);
            lp.setMargins(4, 4, 4, 4);
            r.addView(cell, lp);
        }
        return r;
    }

    /** A button that sends Flipper-style events: Press, then Short on a quick tap,
     *  or Long followed by Repeat while held, then Release. */
    @SuppressLint("ClickableViewAccessibility")
    private View button(String label, int key, int unit) {
        TextView b = new TextView(this);
        b.setText(label);
        b.setGravity(Gravity.CENTER);
        b.setTextColor(Color.rgb(255, 130, 0));
        b.setTypeface(Typeface.DEFAULT_BOLD);
        b.setTextSize(key == NativeBridge.KEY_OK ? 18 : 22);
        GradientDrawable bg = new GradientDrawable();
        bg.setColor(Color.rgb(45, 45, 45));
        bg.setCornerRadius(unit / 2f);
        bg.setStroke(3, Color.rgb(255, 130, 0));
        b.setBackground(bg);

        final boolean[] longFired = {false};
        final Runnable repeat = new Runnable() {
            @Override
            public void run() {
                NativeBridge.input(key, NativeBridge.TYPE_REPEAT);
                handler.postDelayed(this, REPEAT_MS);
            }
        };
        final Runnable longPress = () -> {
            longFired[0] = true;
            NativeBridge.input(key, NativeBridge.TYPE_LONG);
            handler.postDelayed(repeat, REPEAT_MS);
        };
        b.setOnTouchListener((v, ev) -> {
            switch(ev.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                longFired[0] = false;
                v.setPressed(true);
                bg.setColor(Color.rgb(90, 50, 0));
                NativeBridge.input(key, NativeBridge.TYPE_PRESS);
                handler.postDelayed(longPress, LONG_PRESS_MS);
                return true;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
                v.setPressed(false);
                bg.setColor(Color.rgb(45, 45, 45));
                handler.removeCallbacks(longPress);
                handler.removeCallbacks(repeat);
                if(!longFired[0] && ev.getActionMasked() == MotionEvent.ACTION_UP)
                    NativeBridge.input(key, NativeBridge.TYPE_SHORT);
                NativeBridge.input(key, NativeBridge.TYPE_RELEASE);
                return true;
            default:
                return true;
            }
        });
        return b;
    }

    /* ---- Hardware keys (D-pad, keyboards, the system Back button) ---- */

    private static int mapKey(int keyCode) {
        switch(keyCode) {
        case KeyEvent.KEYCODE_DPAD_UP:
        case KeyEvent.KEYCODE_W:
            return NativeBridge.KEY_UP;
        case KeyEvent.KEYCODE_DPAD_DOWN:
        case KeyEvent.KEYCODE_S:
            return NativeBridge.KEY_DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT:
        case KeyEvent.KEYCODE_A:
            return NativeBridge.KEY_LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT:
        case KeyEvent.KEYCODE_D:
            return NativeBridge.KEY_RIGHT;
        case KeyEvent.KEYCODE_DPAD_CENTER:
        case KeyEvent.KEYCODE_ENTER:
        case KeyEvent.KEYCODE_SPACE:
            return NativeBridge.KEY_OK;
        case KeyEvent.KEYCODE_BACK:
        case KeyEvent.KEYCODE_ESCAPE:
        case KeyEvent.KEYCODE_DEL:
            return NativeBridge.KEY_BACK;
        default:
            return -1;
        }
    }

    @Override
    public boolean onKeyDown(int keyCode, KeyEvent event) {
        int key = mapKey(keyCode);
        if(key < 0) return super.onKeyDown(keyCode, event);
        if(event.getRepeatCount() == 0)
            NativeBridge.input(key, NativeBridge.TYPE_PRESS);
        else
            NativeBridge.input(key, NativeBridge.TYPE_REPEAT);
        return true;
    }

    @Override
    public boolean onKeyUp(int keyCode, KeyEvent event) {
        int key = mapKey(keyCode);
        if(key < 0) return super.onKeyUp(keyCode, event);
        NativeBridge.input(key, NativeBridge.TYPE_SHORT);
        NativeBridge.input(key, NativeBridge.TYPE_RELEASE);
        return true;
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        /* Gesture/system Back = the Flipper's Back button (quit from the main menu). */
        NativeBridge.input(NativeBridge.KEY_BACK, NativeBridge.TYPE_SHORT);
    }
}
