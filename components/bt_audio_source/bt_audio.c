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
#include <string.h>

static const char *TAG = "bt_audio";

static atomic_int gain_fp;          // target gain, fixed-point: 1000 = 1.0x
static int        current_gain_fp;  // actual applied gain, ramped toward target
static atomic_int conn_state;        // bt_conn_state_t
static uint8_t    pending_bda[6];
static int        pending_connect = 0;
static atomic_int audio_level;

// ---- Audio source -------------------------------------------------------

static bt_audio_source_cb_t source_cb = NULL;

void bt_audio_set_source(bt_audio_source_cb_t cb)
{
    source_cb = cb;
}

// Test tone fallback — used when no source callback is registered.
#define SAMPLE_RATE   44100
#define TONE_HZ       440
#define TWO_PI        6.28318530718f
static float tone_phase = 0.0f;
static const float tone_phase_inc = TWO_PI * TONE_HZ / SAMPLE_RATE;

#define GAIN_STEP 10

// Called by the A2DP stack to pull PCM data. Stereo 16-bit signed @ 44100Hz.
static int32_t source_data_cb(uint8_t *buf, int32_t len)
{
    int16_t *out    = (int16_t *)buf;
    int      frames = len / 4;  // stereo 16-bit
    int      target = atomic_load(&gain_fp);

    // Fill buffer from source callback or test tone
    if (source_cb) {
        source_cb(out, frames);
    } else {
        for (int i = 0; i < frames; i++) {
            int32_t s = (int32_t)(sinf(tone_phase) * 32767.0f);
            tone_phase += tone_phase_inc;
            if (tone_phase >= TWO_PI) tone_phase -= TWO_PI;
            out[i * 2]     = (int16_t)s;
            out[i * 2 + 1] = (int16_t)s;
        }
    }

    // Apply master output gain and measure level
    int32_t peak = 0;
    for (int i = 0; i < frames; i++) {
        if      (current_gain_fp < target - GAIN_STEP) current_gain_fp += GAIN_STEP;
        else if (current_gain_fp > target + GAIN_STEP) current_gain_fp -= GAIN_STEP;
        else                                            current_gain_fp  = target;

        int32_t l = (int32_t)out[i * 2]     * current_gain_fp / 1000;
        int32_t r = (int32_t)out[i * 2 + 1] * current_gain_fp / 1000;
        if (l >  32767) l =  32767;
        if (l < -32767) l = -32767;
        if (r >  32767) r =  32767;
        if (r < -32767) r = -32767;
        out[i * 2]     = (int16_t)l;
        out[i * 2 + 1] = (int16_t)r;

        int32_t abs_l = l < 0 ? -l : l;
        int32_t abs_r = r < 0 ? -r : r;
        int32_t p = abs_l > abs_r ? abs_l : abs_r;
        if (p > peak) peak = p;
    }

    int new_level;
    if (peak == 0) {
        new_level = 0;
    } else {
        float dBFS = 20.0f * log10f((float)peak / 32767.0f);
        new_level = (int)((dBFS + 40.0f) * 2.5f);
        if (new_level < 0)   new_level = 0;
        if (new_level > 100) new_level = 100;
    }
    int current  = atomic_load(&audio_level);
    int smoothed = (new_level > current) ? new_level : (current * 7 + new_level) / 8;
    atomic_store(&audio_level, smoothed);

    return len;
}

// ---- A2DP / GAP callbacks -----------------------------------------------

static void a2d_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *param)
{
    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT:
        switch (param->conn_stat.state) {
        case ESP_A2D_CONNECTION_STATE_CONNECTING:
            atomic_store(&conn_state, BT_STATE_CONNECTING);
            break;
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            atomic_store(&conn_state, BT_STATE_CONNECTED);
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
            atomic_store(&conn_state, BT_STATE_DISCONNECTED);
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            atomic_store(&conn_state, BT_STATE_DISCONNECTED);
            if (pending_connect) {
                pending_connect = 0;
                atomic_store(&conn_state, BT_STATE_CONNECTING);
                esp_a2d_source_connect(pending_bda);
            }
            break;
        default:
            break;
        }
        break;
    case ESP_A2D_AUDIO_STATE_EVT:
        ESP_LOGI(TAG, "audio %s", param->audio_stat.state == ESP_A2D_AUDIO_STATE_STARTED
                                   ? "started" : "stopped");
        break;
    case ESP_A2D_MEDIA_CTRL_ACK_EVT:
        if (param->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY
                && param->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS)
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
        break;
    default:
        break;
    }
}

static void gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    if (event == ESP_BT_GAP_AUTH_CMPL_EVT) {
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
            ESP_LOGI(TAG, "paired: %s", param->auth_cmpl.device_name);
        else
            ESP_LOGE(TAG, "pairing failed: %d", param->auth_cmpl.stat);
    }
}

// ---- Public API ---------------------------------------------------------

void bt_audio_set_volume(int v)
{
    // Map 0-100 → -40dB to +6.8dB logarithmically
    float gain;
    if (v == 0) {
        gain = 0.0f;
    } else {
        float dB = -40.0f + v * 0.468f;
        gain = powf(10.0f, dB / 20.0f);
    }
    atomic_store(&gain_fp, (int)(gain * 1000.0f));
}

void bt_audio_init(void)
{
    atomic_init(&audio_level, 0);
    atomic_init(&gain_fp, 1000);
    atomic_init(&conn_state, BT_STATE_IDLE);
    current_gain_fp = 1000;

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    bt_cfg.mode = ESP_BT_MODE_CLASSIC_BT;
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    bd_cfg.ssp_en = false;
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bd_cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
    esp_bt_pin_code_t pin_code = {'0', '0', '0', '0'};
    esp_bt_gap_set_pin(pin_type, 4, pin_code);

    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gap_cb));
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2d_cb));
    ESP_ERROR_CHECK(esp_a2d_source_init());
    ESP_ERROR_CHECK(esp_a2d_source_register_data_callback(source_data_cb));

    esp_bt_gap_set_device_name("bt-mixer");
}

void bt_audio_connect(uint8_t *bda)
{
    bt_conn_state_t current = (bt_conn_state_t)atomic_load(&conn_state);
    if (current == BT_STATE_CONNECTING) return;
    if (current == BT_STATE_CONNECTED) {
        // Store target and wait for disconnect callback before connecting
        memcpy(pending_bda, bda, 6);
        pending_connect = 1;
        esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
        esp_a2d_source_disconnect(bda);
        return;
    }
    atomic_store(&conn_state, BT_STATE_CONNECTING);
    esp_a2d_source_connect(bda);
}

bt_conn_state_t bt_audio_get_state(void)
{
    return (bt_conn_state_t)atomic_load(&conn_state);
}

int bt_audio_get_level(void)
{
    return atomic_load(&audio_level);
}
