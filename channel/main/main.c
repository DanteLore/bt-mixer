#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "encoder.h"
#include "bt_audio.h"
#include "esp_gap_bt_api.h"

static const char *TAG = "channel";

// Channel identity — set this per board (1, 2, 3 ...).
// Will eventually be read from hardware strapping pins or NVS.
#define CHANNEL_ID  1

#define NVS_NAMESPACE  "channel"
#define NVS_KEY_VOLUME "volume"

#define POLL_INTERVAL_US   20000   // 50Hz encoder poll + level read
#define NVS_DEBOUNCE_US  2000000   // save volume 2s after knob stops

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

    int volume = load_volume();

    encoder_init(33, 32, 27);  // CLK, DT, SW

    char device_name[16];
    snprintf(device_name, sizeof(device_name), "bt-mixer-ch%d", CHANNEL_ID);
    bt_audio_init();
    esp_bt_gap_set_device_name(device_name);
    bt_audio_set_volume(volume);

    ESP_LOGI(TAG, "channel %d starting, volume %d", CHANNEL_ID, volume);

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

        // TODO: respond to SPI master poll with current PCM frame + volume

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
