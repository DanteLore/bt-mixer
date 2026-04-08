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
#include "scan_mode.h"

// Bar geometry (landscape 320x170)
#define BAR_X   20
#define BAR_W  280
#define BAR_H    8

#define VOL_TEXT_Y   8
#define VOL_BAR_Y   24
#define SIG_TEXT_Y  48
#define SIG_BAR_Y   64
// Conn status shares the VOL row, right-aligned
#define CONN_TEXT_X 176
#define CONN_TEXT_Y VOL_TEXT_Y

#define RECONNECT_US  7000000   // retry every 7s when disconnected

#define DRAW_INTERVAL_US  50000   // 20Hz

#define NVS_NAMESPACE  "bt-mixer"
#define NVS_KEY_VOLUME "volume"
#define NVS_KEY_BDA    "bda"
#define NVS_KEY_DEVNAME "devname"

// Colour thresholds for SIG bar
#define SIG_GREEN_PX  ((BAR_W * 75) / 100)
#define SIG_AMBER_PX  ((BAR_W * 90) / 100)

typedef enum { MODE_DISPLAY, MODE_SCAN } app_mode_t;

// ---- NVS ----------------------------------------------------------------

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

static int load_bda(uint8_t *bda)
{
    nvs_handle_t h;
    size_t len = 6;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        esp_err_t err = nvs_get_blob(h, NVS_KEY_BDA, bda, &len);
        nvs_close(h);
        if (err == ESP_OK && len == 6) return 1;
    }
    return 0;
}

static void save_bda(uint8_t *bda, const char *name)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, NVS_KEY_BDA, bda, 6);
        nvs_set_str(h, NVS_KEY_DEVNAME, name ? name : "");
        nvs_commit(h);
        nvs_close(h);
    }
}

static void load_devname(char *buf, size_t len)
{
    nvs_handle_t h;
    buf[0] = '\0';
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, NVS_KEY_DEVNAME, buf, &len);
        nvs_close(h);
    }
}

// ---- Display helpers ----------------------------------------------------

static float volume_to_db(int v)
{
    if (v == 0) return -99.0f;
    return -40.0f + v * 0.468f;
}

static void format_vol_label(char *buf, int volume)
{
    if (volume == 0)
        snprintf(buf, 16, "VOL   MUTE  ");
    else
        snprintf(buf, 16, "VOL %+.1fdB  ", volume_to_db(volume));
}

static void format_sig_label(char *buf, int level)
{
    if (level == 0)
        snprintf(buf, 16, "SIG          ");
    else {
        float dBFS = level / 2.5f - 40.0f;
        snprintf(buf, 16, "SIG %+.1fdB  ", dBFS);
    }
}

static void update_vol_bar(int value, int prev_value)
{
    int fill_w      = (BAR_W * value)      / 100;
    int prev_fill_w = (BAR_W * prev_value) / 100;

    if (fill_w > prev_fill_w)
        st7789_fill_rect(BAR_X + prev_fill_w, VOL_BAR_Y, fill_w - prev_fill_w, BAR_H, COLOR_GREEN);
    else if (fill_w < prev_fill_w)
        st7789_fill_rect(BAR_X + fill_w, VOL_BAR_Y, prev_fill_w - fill_w, BAR_H, RGB565(40, 40, 40));
}

static void update_sig_bar(int value, int prev_value)
{
    int fill_w      = (BAR_W * value)      / 100;
    int prev_fill_w = (BAR_W * prev_value) / 100;

    if (fill_w > prev_fill_w) {
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


static void draw_conn_state(bt_conn_state_t state)
{
    char     buf[14];
    uint16_t colour;

    if (state == BT_STATE_CONNECTED) {
        snprintf(buf, sizeof(buf), "%-13s", scan_mode_get_name());
        colour = COLOR_GREEN;
    } else {
        const char *label;
        switch (state) {
        case BT_STATE_CONNECTING:   label = "CONNECTING..."; colour = COLOR_YELLOW; break;
        case BT_STATE_DISCONNECTED: label = "DISCONNECTED "; colour = COLOR_RED;    break;
        default:                    label = "             "; colour = COLOR_WHITE;   break;
        }
        snprintf(buf, sizeof(buf), "%s", label);
    }

    st7789_draw_string(CONN_TEXT_X, CONN_TEXT_Y, buf, colour, COLOR_BLACK);
}

static void draw_display_mode(int volume, int level)
{
    char buf[16];
    format_vol_label(buf, volume);
    st7789_draw_string(8, VOL_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
    st7789_fill_rect(BAR_X, VOL_BAR_Y, BAR_W, BAR_H, RGB565(40, 40, 40));
    update_vol_bar(volume, 0);

    format_sig_label(buf, level);
    st7789_draw_string(8, SIG_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
    st7789_fill_rect(BAR_X, SIG_BAR_Y, BAR_W, BAR_H, RGB565(40, 40, 40));
    update_sig_bar(level, 0);
    draw_conn_state(bt_audio_get_state());
}

// ---- Button debounce ----------------------------------------------------

// Returns 1 on the rising edge of a button press (low-to-not-low transition)
static int button_edge(void)
{
    static int was_pressed = 0;
    int pressed = encoder_button_pressed();
    int edge = (!was_pressed && pressed);
    was_pressed = pressed;
    return edge;
}

// ---- app_main -----------------------------------------------------------

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

    encoder_init(33, 27, 14);  // CLK, DT, SW
    bt_audio_init();
    bt_audio_set_volume(volume);
    scan_mode_init();

    // Restore last known speaker name and auto-connect
    uint8_t saved_bda[6];
    char    saved_name[20] = {0};
    load_devname(saved_name, sizeof(saved_name));
    scan_mode_set_name(saved_name);
    if (load_bda(saved_bda))
        bt_audio_connect(saved_bda);

    int last_drawn_volume  = -1;
    int last_drawn_level   = -1;
    int64_t last_draw_us      = 0;
    int     saved_volume      = volume;
    int64_t volume_changed_us = 0;
    app_mode_t      mode           = MODE_DISPLAY;
    bt_conn_state_t last_conn_state = BT_STATE_IDLE;
    int64_t         disconnected_us = 0;

    draw_display_mode(volume, 0);
    last_drawn_volume = volume;
    last_drawn_level  = 0;

    while (1) {
        int d    = encoder_get_delta();
        int btn  = button_edge();

        if (mode == MODE_SCAN) {
            int result = scan_mode_tick(d, btn);
            if (result != 0) {
                if (result == 1) {
                    // Device selected — save and connect
                    uint8_t *bda = scan_mode_get_bda();
                    save_bda(bda, scan_mode_get_name());
                    bt_audio_connect(bda);
                }
                // result == -1: back pressed, just return to display
                mode = MODE_DISPLAY;
                st7789_fill(COLOR_BLACK);
                draw_display_mode(volume, bt_audio_get_level());
                last_drawn_volume = volume;
                last_drawn_level  = bt_audio_get_level();
            }
        } else {
            // Display mode — button enters scan mode
            if (btn) {
                mode = MODE_SCAN;
                scan_mode_enter();
            }

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
                    char buf[16];
                    format_vol_label(buf, volume);
                    st7789_draw_string(8, VOL_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
                    update_vol_bar(volume, last_drawn_volume);
                    last_drawn_volume = volume;
                }

                if (level != last_drawn_level) {
                    char buf[16];
                    format_sig_label(buf, level);
                    st7789_draw_string(8, SIG_TEXT_Y, buf, COLOR_WHITE, COLOR_BLACK);
                    update_sig_bar(level, last_drawn_level);
                    last_drawn_level = level;
                }

                if (volume != saved_volume && volume_changed_us > 0
                        && now - volume_changed_us >= 2000000) {
                    save_volume(volume);
                    saved_volume = volume;
                }

                // Redraw connection state when it changes
                bt_conn_state_t conn = bt_audio_get_state();
                if (conn != last_conn_state) {
                    draw_conn_state(conn);
                    if (conn == BT_STATE_DISCONNECTED)
                        disconnected_us = now;
                    last_conn_state = conn;
                }

                // Auto-reconnect when disconnected (not already connecting)
                if (conn == BT_STATE_DISCONNECTED && disconnected_us > 0
                        && now - disconnected_us >= RECONNECT_US
                        && last_conn_state != BT_STATE_CONNECTING) {
                    if (load_bda(saved_bda))
                        bt_audio_connect(saved_bda);
                    disconnected_us = now;
                }

                last_draw_us = now;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
