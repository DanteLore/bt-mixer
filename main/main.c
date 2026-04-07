#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "st7789.h"
#include "encoder.h"

// Bar geometry (landscape 320x170)
#define BAR_X   20
#define BAR_Y   24
#define BAR_W  280
#define BAR_H    8

#define DRAW_INTERVAL_US  50000   // 20Hz

static void draw_volume(int volume, int prev_volume)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "VOLUME: %3d%%", volume);
    st7789_draw_string(8, 8, buf, COLOR_WHITE, COLOR_BLACK);

    int fill_w      = (BAR_W * volume)      / 100;
    int prev_fill_w = (BAR_W * prev_volume) / 100;

    if (fill_w > prev_fill_w) {
        // Grew — paint the new green slice
        st7789_fill_rect(BAR_X + prev_fill_w, BAR_Y, fill_w - prev_fill_w, BAR_H, COLOR_GREEN);
    } else if (fill_w < prev_fill_w) {
        // Shrank — clear the removed slice back to grey
        st7789_fill_rect(BAR_X + fill_w, BAR_Y, prev_fill_w - fill_w, BAR_H, RGB565(40, 40, 40));
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

    st7789_init();
    st7789_fill(COLOR_BLACK);

    encoder_init();

    int volume = 50;
    int last_drawn_volume = -1;
    int64_t last_draw_us = 0;

    // Initial full draw: pass prev_volume = 0 so the whole bar is painted from scratch
    st7789_fill_rect(BAR_X, BAR_Y, BAR_W, BAR_H, RGB565(40, 40, 40));
    draw_volume(volume, 0);
    last_drawn_volume = volume;

    while (1) {
        int d = encoder_get_delta();
        if (d != 0) {
            volume += d;
            if (volume < 0)   volume = 0;
            if (volume > 100) volume = 100;
        }

        int64_t now = esp_timer_get_time();
        if (volume != last_drawn_volume && now - last_draw_us >= DRAW_INTERVAL_US) {
            draw_volume(volume, last_drawn_volume);
            last_drawn_volume = volume;
            last_draw_us = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
