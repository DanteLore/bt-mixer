#pragma once

void bt_audio_init(void);
void bt_audio_set_volume(int volume);  // 0-100

// Returns current signal level 0-100, with fast attack and slow decay.
int bt_audio_get_level(void);
