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
void bt_audio_connect(uint8_t *bda);   // connect to BT speaker by address

bt_conn_state_t bt_audio_get_state(void);

// Returns current signal level 0-100, with fast attack and slow decay.
int bt_audio_get_level(void);
