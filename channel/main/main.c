#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "encoder.h"
#include "bt_audio.h"
#include "uart_audio_slave.h"
#include "esp_gap_bt_api.h"

static const char *TAG = "channel";

#define NVS_NAMESPACE    "channel"
#define NVS_KEY_VOLUME   "volume"
#define NVS_KEY_CHAN_ID  "chan_id"

#define NVS_DEBOUNCE_US  2000000   // save volume 2s after knob stops

static int load_channel_id(void)
{
    nvs_handle_t h;
    int32_t id = 0;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, NVS_KEY_CHAN_ID, &id);
        nvs_close(h);
    }
    if (id <= 0) {
        ESP_LOGW(TAG, "channel ID not set — hold button at boot to configure");
        id = 1;
    }
    return (int)id;
}

static void save_channel_id(int id)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_CHAN_ID, (int32_t)id);
        nvs_commit(h);
        nvs_close(h);
    }
}

// If button is held at boot: count subsequent presses to set channel ID.
// Each press increments the channel. Saves and exits after 3s of no presses.
static int provision_channel_id(void)
{
    if (!encoder_button_pressed()) return -1;

    ESP_LOGW(TAG, "provisioning mode — press button to set channel number");

    // Wait for button release
    while (encoder_button_pressed()) vTaskDelay(pdMS_TO_TICKS(10));

    int id = 0;
    int64_t last_press_us = esp_timer_get_time();

    while (esp_timer_get_time() - last_press_us < 3000000) {
        if (encoder_button_pressed()) {
            id++;
            ESP_LOGW(TAG, "channel = %d", id);
            last_press_us = esp_timer_get_time();
            while (encoder_button_pressed()) vTaskDelay(pdMS_TO_TICKS(10));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (id <= 0) id = 1;
    save_channel_id(id);
    ESP_LOGW(TAG, "channel ID saved: %d", id);
    return id;
}

static int load_volume(void)
{
    nvs_handle_t h;
    int32_t v = 75;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_i32(h, NVS_KEY_VOLUME, &v);
        nvs_close(h);
    }
    return (int)v;
}

static void save_volume(int volume)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_i32(h, NVS_KEY_VOLUME, (int32_t)volume);
        nvs_commit(h);
        nvs_close(h);
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    encoder_init(33, 32, 27);  // CLK, DT, SW — must come before provisioning check

    int channel_id;
    int provisioned = provision_channel_id();
    if (provisioned > 0)
        channel_id = provisioned;
    else
        channel_id = load_channel_id();

    int volume = load_volume();

    char device_name[16];
    snprintf(device_name, sizeof(device_name), "bt-mixer-ch%d", channel_id);
    bt_audio_init();
    esp_bt_gap_set_device_name(device_name);
    bt_audio_set_volume(volume);
    uart_audio_slave_init();

    ESP_LOGI(TAG, "channel %d starting, volume %d", channel_id, volume);

    int     saved_volume      = volume;
    int64_t volume_changed_us = 0;
    int     last_button       = 0;

    while (1) {
        int d = encoder_get_delta();
        if (d != 0) {
            volume += d;
            if (volume < 0)   volume = 0;
            if (volume > 100) volume = 100;
            bt_audio_set_volume(volume);
            volume_changed_us = esp_timer_get_time();
            ESP_LOGI(TAG, "dial delta=%+d  volume=%d", d, volume);
        }

        int btn = encoder_button_pressed();
        if (btn && !last_button)
            ESP_LOGI(TAG, "button pressed");
        else if (!btn && last_button)
            ESP_LOGI(TAG, "button released");
        last_button = btn;

        // NVS save after knob settles
        if (volume != saved_volume && volume_changed_us > 0
                && esp_timer_get_time() - volume_changed_us >= NVS_DEBOUNCE_US) {
            save_volume(volume);
            saved_volume = volume;
            ESP_LOGI(TAG, "volume saved: %d", volume);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
