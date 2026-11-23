#pragma once

/* Notifications: on a phone, the ones that would buzz the Flipper vibrate. */
typedef struct {
    int vibrate_ms; /* 0 = LED blink only (ignored) */
} NotificationSequence;

typedef struct NotificationApp NotificationApp;

void notification_message(NotificationApp* app, const NotificationSequence* sequence);

extern const NotificationSequence sequence_blink_green_10;
extern const NotificationSequence sequence_blink_cyan_10;
extern const NotificationSequence sequence_blink_magenta_100;
extern const NotificationSequence sequence_single_vibro;
extern const NotificationSequence sequence_success;
extern const NotificationSequence sequence_semi_success;
extern const NotificationSequence sequence_error;
