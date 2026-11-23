package com.echoids.app;

import android.content.Intent;
import android.provider.Settings;
import android.view.View;
import android.widget.LinearLayout;

/** Hold an NFC card to the phone: its echoid comes out to battle. */
final class CardScreen extends Screen {
    CardScreen(MainActivity app) {
        super(app);
    }

    @Override
    String title() {
        return "Scan a card";
    }

    @Override
    boolean autoRefresh() {
        return true; /* notice NFC being switched on */
    }

    @Override
    View build() {
        LinearLayout col = Ui.column(app);
        LinearLayout card = Ui.card(app);
        if(!app.cards.supported()) {
            card.addView(Ui.title(app, "No NFC reader"));
            card.addView(Ui.text(app, "This phone can't read cards. On a Flipper Zero, Scan card reads NFC and 125 kHz RFID cards.", 15, Ui.MUTED));
        } else if(!app.cards.enabled()) {
            card.addView(Ui.title(app, "NFC is off"));
            card.addView(Ui.text(app, "Turn on NFC to scan cards.", 15, Ui.MUTED));
            card.addView(Ui.space(app, 1, 12));
            card.addView(Ui.button(app, "Open NFC settings", v -> {
                try {
                    app.startActivity(new Intent(Settings.ACTION_NFC_SETTINGS));
                } catch(RuntimeException e) {
                    app.startActivity(new Intent(Settings.ACTION_WIRELESS_SETTINGS));
                }
            }));
        } else {
            card.addView(Ui.title(app, "📇 Hold a card to the back of the phone"));
            card.addView(Ui.text(app,
                "Any NFC card, tag or sticker hides an echoid, always the same one for the same card. " +
                "Catch it or beat it and that card is spent; run away and you can try again.", 15, Ui.MUTED));
        }
        col.addView(card);
        return app.page(col);
    }
}
