package com.echoids.app;

import android.view.View;

/** One screen of the app. Screens rebuild their view from the engine's state. */
abstract class Screen {
    final MainActivity app;

    Screen(MainActivity app) {
        this.app = app;
    }

    /** Builds the view for the current game state. */
    abstract View build();

    /** Shown in the action bar. */
    abstract String title();

    /** Rebuild about once a second while shown (for live data). */
    boolean autoRefresh() {
        return false;
    }

    /** Handle Back; return true if handled (the screen stays). */
    boolean onBack() {
        return false;
    }
}
