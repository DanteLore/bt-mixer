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

// Source only — connect to a BT speaker by address
void bt_audio_connect(uint8_t *bda);

bt_conn_state_t bt_audio_get_state(void);

// Returns current signal level 0-100, fast attack / slow decay.
int bt_audio_get_level(void);
