#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "st7789.h"
#include "encoder.h"
#include "bt_audio.h"

// Bar geometry (landscape 320x170)
#define BAR_X   20
#define BAR_W  280
#define BAR_H    8

#define VOL_TEXT_Y   8
#define VOL_BAR_Y   24
#define SIG_TEXT_Y  48
#define SIG_BAR_Y   64

#define DRAW_INTERVAL_US  50000   // 20Hz

#define NVS_NAMESPACE  "bt-mixer"
#define NVS_KEY_VOLUME "volume"

static int load_volume(void)
{
    nvs_handle_t h;
    int32_t v = 50;
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

// Paints only the slice of a bar that changed since last draw.
static void update_bar(int y, int value, int prev_value)
{
    int fill_w      = (BAR_W * value)      / 100;
    int prev_fill_w = (BAR_W * prev_value) / 100;

    if (fill_w > prev_fill_w)
        st7789_fill_rect(BAR_X + prev_fill_w, y, fill_w - prev_fill_w, BAR_H, COLOR_GREEN);
    else if (fill_w < prev_fill_w)
        st7789_fill_rect(BAR_X + fill_w, y, prev_fill_w - fill_w, BAR_H, RGB565(40, 40, 40));
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

    st7789_init();
    st7789_fill(COLOR_BLACK);

    encoder_init();
    bt_audio_init();
    bt_audio_set_volume(volume);

    int last_drawn_volume = -1;
    int last_drawn_level  = -1;
    int64_t last_draw_us  = 0;

    // Initial draw
    char buf[16];
    snprintf(buf, sizeof(buf), "VOL: %3d%%", volume);
    st7789_draw_string(8, VOL_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
    st7789_fill_rect(BAR_X, VOL_BAR_Y, BAR_W, BAR_H, RGB565(40, 40, 40));
    update_bar(VOL_BAR_Y, volume, 0);

    st7789_draw_string(8, SIG_TEXT_Y, "SIG:   0%", COLOR_WHITE, COLOR_BLACK);
    st7789_fill_rect(BAR_X, SIG_BAR_Y, BAR_W, BAR_H, RGB565(40, 40, 40));

    last_drawn_volume = volume;
    last_drawn_level  = 0;

    while (1) {
        int d = encoder_get_delta();
        if (d != 0) {
            volume += d;
            if (volume < 0)   volume = 0;
            if (volume > 100) volume = 100;
        }

        int64_t now = esp_timer_get_time();
        if (now - last_draw_us >= DRAW_INTERVAL_US) {
            int level = bt_audio_get_level();

            if (volume != last_drawn_volume) {
                bt_audio_set_volume(volume);
                save_volume(volume);
                snprintf(buf, sizeof(buf), "VOL: %3d%%", volume);
                st7789_draw_string(8, VOL_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
                update_bar(VOL_BAR_Y, volume, last_drawn_volume);
                last_drawn_volume = volume;
            }

            if (level != last_drawn_level) {
                snprintf(buf, sizeof(buf), "SIG: %3d%%", level);
                st7789_draw_string(8, SIG_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
                update_bar(SIG_BAR_Y, level, last_drawn_level);
                last_drawn_level = level;
            }

            last_draw_us = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
