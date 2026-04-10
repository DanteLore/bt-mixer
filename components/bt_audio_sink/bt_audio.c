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
static int        current_gain_fp;  // ramped toward target each callback
static atomic_int audio_level;
static atomic_int conn_state;
static atomic_int current_volume;   // 0-100, for SPI frame header

#define GAIN_STEP 10

// ---- PCM double-buffer for SPI slave ------------------------------------
// data_cb writes gain-adjusted samples into the fill half; when a frame's
// worth accumulates, the halves swap. spi_audio_slave reads the ready half.
// Size must match SPI_AUDIO_FRAME_SAMPLES in spi_audio.h.

#define PCM_FRAME_SAMPLES  128   // stereo pairs per frame

static int16_t pcm_buf[2][PCM_FRAME_SAMPLES * 2];  // stereo interleaved
static volatile int write_half = 0;
static int fill_pos = 0;
static void (*frame_cb)(void) = NULL;

// ---- A2DP sink data callback --------------------------------------------

static void data_cb(const uint8_t *buf, uint32_t len)
{
    const int16_t *samples = (const int16_t *)buf;
    int n      = len / 2;   // total int16 values (not stereo pairs)
    int target = atomic_load(&gain_fp);

    int32_t peak = 0;
    for (int i = 0; i < n; i++) {
        if      (current_gain_fp < target - GAIN_STEP) current_gain_fp += GAIN_STEP;
        else if (current_gain_fp > target + GAIN_STEP) current_gain_fp -= GAIN_STEP;
        else                                            current_gain_fp  = target;

        int32_t s = (int32_t)samples[i] * current_gain_fp / 1000;
        if (s >  32767) s =  32767;
        if (s < -32767) s = -32767;

        pcm_buf[write_half][fill_pos] = (int16_t)s;
        if (++fill_pos >= PCM_FRAME_SAMPLES * 2) {
            write_half = 1 - write_half;
            fill_pos   = 0;
            if (frame_cb) frame_cb();
        }

        int32_t abs_s = s < 0 ? -s : s;
        if (abs_s > peak) peak = abs_s;
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
    int cur      = atomic_load(&audio_level);
    int smoothed = (new_level > cur) ? new_level : (cur * 7 + new_level) / 8;
    atomic_store(&audio_level, smoothed);
}

// ---- Callbacks ----------------------------------------------------------

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
            ESP_LOGI(TAG, "connected");
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTING:
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            atomic_store(&conn_state, BT_STATE_DISCONNECTED);
            ESP_LOGI(TAG, "disconnected");
            break;
        default:
            break;
        }
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
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS)
            ESP_LOGI(TAG, "paired: %s", param->auth_cmpl.device_name);
        else
            ESP_LOGE(TAG, "pairing failed: %d", param->auth_cmpl.stat);
    }
}

// ---- Public API ---------------------------------------------------------

void bt_audio_set_volume(int v)
{
    atomic_store(&current_volume, v);
    float gain;
    if (v == 0) {
        gain = 0.0f;
    } else {
        float dB = -40.0f + v * 0.468f;
        gain = powf(10.0f, dB / 20.0f);
    }
    atomic_store(&gain_fp, (int)(gain * 1000.0f));
}

int bt_audio_get_volume(void)
{
    return atomic_load(&current_volume);
}

void bt_audio_copy_frame(int16_t *dst, int n_stereo_samples)
{
    // Read from the half not currently being written
    int read_half = 1 - write_half;
    int n = n_stereo_samples * 2;
    if (n > PCM_FRAME_SAMPLES * 2) n = PCM_FRAME_SAMPLES * 2;
    memcpy(dst, pcm_buf[read_half], n * sizeof(int16_t));
}

void bt_audio_init(void)
{
    atomic_init(&audio_level,    0);
    atomic_init(&gain_fp,        1000);
    atomic_init(&conn_state,     BT_STATE_IDLE);
    atomic_init(&current_volume, 75);
    current_gain_fp = 1000;
    memset(pcm_buf, 0, sizeof(pcm_buf));

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
    ESP_ERROR_CHECK(esp_a2d_sink_init());
    ESP_ERROR_CHECK(esp_a2d_sink_register_data_callback(data_cb));

    // Device name set per-channel in app_main
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

bt_conn_state_t bt_audio_get_state(void)
{
    return (bt_conn_state_t)atomic_load(&conn_state);
}

int bt_audio_get_level(void)
{
    return atomic_load(&audio_level);
}

void bt_audio_set_frame_callback(void (*cb)(void))
{
    frame_cb = cb;
}
