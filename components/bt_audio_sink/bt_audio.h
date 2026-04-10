#pragma once

#include <stdint.h>

typedef enum {
    BT_STATE_IDLE,
    BT_STATE_CONNECTING,
    BT_STATE_CONNECTED,
    BT_STATE_DISCONNECTED,
} bt_conn_state_t;

void bt_audio_init(void);
void bt_audio_set_volume(int volume);  // 0-100
int  bt_audio_get_volume(void);        // last value passed to set_volume

bt_conn_state_t bt_audio_get_state(void);

// Returns current signal level 0-100, fast attack / slow decay.
int bt_audio_get_level(void);

// Copy n_stereo_samples gain-adjusted stereo pairs into dst.
// Always succeeds — returns silence until BT audio arrives.
void bt_audio_copy_frame(int16_t *dst, int n_stereo_samples);

// Register a callback fired each time a complete 128-sample frame is ready.
// Called from the BT audio task — must not block.
void bt_audio_set_frame_callback(void (*cb)(void));
