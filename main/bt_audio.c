#include "bt_audio.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_a2dp_legacy_api.h"
#include "esp_log.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "bt_audio";
static atomic_int audio_level;
static atomic_int gain_fp;   // fixed-point: 1000 = 1.0x gain

void bt_audio_set_volume(int v)
{
    // Map 0-100 → -40dB to +12dB logarithmically.
    // v=0  → silence
    // v=77 → 0dB (unity gain, 1.0x)
    // v=100 → +12dB (~4x boost, for quiet sources)
    float gain;
    if (v == 0) {
        gain = 0.0f;
    } else {
        float dB = -40.0f + v * 0.468f;  // 100 → +6.8dB, 0 → -40dB
        gain = powf(10.0f, dB / 20.0f);
    }
    atomic_store(&gain_fp, (int)(gain * 1000.0f));
}

static void data_cb(const uint8_t *buf, uint32_t len)
{
    // Decoded PCM: signed 16-bit stereo
    const int16_t *samples = (const int16_t *)buf;
    int n   = len / 2;
    int gfp = atomic_load(&gain_fp);

    int32_t peak = 0;
    for (int i = 0; i < n; i++) {
        int32_t s = (int32_t)samples[i] * gfp / 1000;
        if (s >  32767) s =  32767;
        if (s < -32767) s = -32767;
        if (s < 0) s = -s;
        if (s > peak) peak = s;
    }

    int new_level;
    if (peak == 0) {
        new_level = 0;
    } else {
        // Map 0dBFS (peak=32767) → 100, -40dBFS → 0
        float dBFS = 20.0f * log10f((float)peak / 32767.0f);
        new_level = (int)((dBFS + 40.0f) * 2.5f);
        if (new_level < 0)   new_level = 0;
        if (new_level > 100) new_level = 100;
    }
    int current   = atomic_load(&audio_level);
    // Fast attack, slow decay
    int smoothed  = (new_level > current) ? new_level : (current * 7 + new_level) / 8;
    atomic_store(&audio_level, smoothed);
}

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        ESP_LOGI(TAG, "A2DP %s", param->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED
                                  ? "connected" : "disconnected");
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "audio %s", param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED
                                   ? "started" : "stopped");
        break;
    default:
        break;
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_AUTH_CMPL_EVT) {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(TAG, "paired: %s", param->auth_cmpl.device_name);
        } else {
            ESP_LOGE(TAG, "pairing failed: %d", param->auth_cmpl.stat);
        }
    }
}

void bt_audio_init(void)
{
    atomic_init(&audio_level, 0);
    atomic_init(&gain_fp, 1000);   // unity until bt_audio_set_volume() is called

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bd_cfg.ssp_en = false;   // legacy pairing — simpler, no confirmation dialogs
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bd_cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    // Fixed PIN "0000" — phone will prompt once, then remember the pairing
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2d_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_init());
    ESP_ERROR_CHECK(esp_a2d_sink_register_data_callback(data_cb));

    esp_bt_gap_set_device_name("bt-mixer");
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

int bt_audio_get_level(void)
{
    return atomic_load(&audio_level);
}
