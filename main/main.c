#include <stdio.h>
#include <math.h>
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

// Thresholds for SIG bar colour (as bar pixel width out of BAR_W)
#define SIG_GREEN_PX  ((BAR_W * 75) / 100)   // 0–75%
#define SIG_AMBER_PX  ((BAR_W * 90) / 100)   // 75–90%
                                               // 90–100% = red

static int load_volume(void)
{
    nvs_handle_t h;
    int32_t v = 75;   // ~0dB unity gain on first boot
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

static float volume_to_db(int v)
{
    if (v == 0) return -99.0f;
    float dB = -40.0f + v * 0.468f;
    return dB;
}

static void format_vol_label(char *buf, int volume)
{
    if (volume == 0) {
        snprintf(buf, 16, "VOL:    MUTE");
    } else {
        float dB = volume_to_db(volume);
        snprintf(buf, 16, "VOL: %+5.1fdB", dB);
    }
}

static void format_sig_label(char *buf, int level)
{
    if (level == 0) {
        snprintf(buf, 16, "SIG: NO SIGNAL");
    } else {
        float dBFS = level / 2.5f - 40.0f;
        snprintf(buf, 16, "SIG: %+5.1fdB  ", dBFS);
    }
}

// VOL bar: simple green, differential update
static void update_vol_bar(int value, int prev_value)
{
    int fill_w      = (BAR_W * value)      / 100;
    int prev_fill_w = (BAR_W * prev_value) / 100;

    if (fill_w > prev_fill_w)
        st7789_fill_rect(BAR_X + prev_fill_w, VOL_BAR_Y, fill_w - prev_fill_w, BAR_H, COLOR_GREEN);
    else if (fill_w < prev_fill_w)
        st7789_fill_rect(BAR_X + fill_w, VOL_BAR_Y, prev_fill_w - fill_w, BAR_H, RGB565(40, 40, 40));
}

// SIG bar: green/amber/red zones. Redraws the changed region with correct colour.
static void update_sig_bar(int value, int prev_value)
{
    int fill_w      = (BAR_W * value)      / 100;
    int prev_fill_w = (BAR_W * prev_value) / 100;

    if (fill_w > prev_fill_w) {
        // Growing — draw new slice, possibly spanning multiple colour zones
        for (int x = prev_fill_w; x < fill_w; ) {
            int end;
            uint16_t colour;
            if (x < SIG_GREEN_PX) {
                end = SIG_GREEN_PX < fill_w ? SIG_GREEN_PX : fill_w;
                colour = COLOR_GREEN;
            } else if (x < SIG_AMBER_PX) {
                end = SIG_AMBER_PX < fill_w ? SIG_AMBER_PX : fill_w;
                colour = COLOR_YELLOW;
            } else {
                end = fill_w;
                colour = COLOR_RED;
            }
            st7789_fill_rect(BAR_X + x, SIG_BAR_Y, end - x, BAR_H, colour);
            x = end;
        }
    } else if (fill_w < prev_fill_w) {
        st7789_fill_rect(BAR_X + fill_w, SIG_BAR_Y, prev_fill_w - fill_w, BAR_H, RGB565(40, 40, 40));
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

    st7789_init();
    st7789_fill(COLOR_BLACK);

    encoder_init();
    bt_audio_init();
    bt_audio_set_volume(volume);

    int last_drawn_volume  = -1;
    int last_drawn_level   = -1;
    int64_t last_draw_us   = 0;
    int     saved_volume   = volume;
    int64_t volume_changed_us = 0;

    // Initial draw
    char buf[16];
    format_vol_label(buf, volume);
    st7789_draw_string(8, VOL_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
    st7789_fill_rect(BAR_X, VOL_BAR_Y, BAR_W, BAR_H, RGB565(40, 40, 40));
    update_vol_bar(volume, 0);

    format_sig_label(buf, 0);
    st7789_draw_string(8, SIG_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
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
                volume_changed_us = now;
                format_vol_label(buf, volume);
                st7789_draw_string(8, VOL_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
                update_vol_bar(volume, last_drawn_volume);
                last_drawn_volume = volume;
            }

            if (level != last_drawn_level) {
                format_sig_label(buf, level);
                st7789_draw_string(8, SIG_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
                update_sig_bar(level, last_drawn_level);
                last_drawn_level = level;
            }

            // Save to NVS 2 seconds after the knob stops moving
            if (volume != saved_volume && volume_changed_us > 0
                    && now - volume_changed_us >= 2000000) {
                save_volume(volume);
                saved_volume = volume;
            }

            last_draw_us = now;
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
