package com.echoids.app;

import android.app.Activity;
import android.nfc.NfcAdapter;
import android.nfc.Tag;

/**
 * The phone's NFC reader, on only while the card screen is showing. A tapped
 * card's UID goes to the listener on the main thread. (Phones can't read
 * 125 kHz RFID cards: that's Flipper only.)
 */
final class CardReader {
    interface Listener {
        void onCard(byte[] uid);
    }

    private final Activity activity;
    private final NfcAdapter adapter;
    private final Listener listener;
    private boolean on;

    CardReader(Activity activity, Listener listener) {
        this.activity = activity;
        this.listener = listener;
        this.adapter = NfcAdapter.getDefaultAdapter(activity);
    }

    boolean supported() {
        return adapter != null;
    }

    boolean enabled() {
        return adapter != null && adapter.isEnabled();
    }

    /** Turns reading on or off (only call while the activity is resumed to turn it on). */
    void update(boolean want) {
        if(adapter == null || want == on) return;
        try {
            if(want) {
                int flags = NfcAdapter.FLAG_READER_NFC_A | NfcAdapter.FLAG_READER_NFC_B |
                            NfcAdapter.FLAG_READER_NFC_F | NfcAdapter.FLAG_READER_NFC_V |
                            NfcAdapter.FLAG_READER_SKIP_NDEF_CHECK;
                adapter.enableReaderMode(activity, this::tagFound, flags, null);
            } else {
                adapter.disableReaderMode(activity);
            }
            on = want;
        } catch(RuntimeException e) {
            on = false;
        }
    }

    private void tagFound(Tag tag) {
        byte[] id = tag.getId();
        if(id == null || id.length == 0) return;
        activity.runOnUiThread(() -> listener.onCard(id));
    }
}
