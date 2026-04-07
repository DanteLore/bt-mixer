#include "scan_mode.h"
#include "bt_audio.h"
#include "st7789.h"

#include "esp_gap_bt_api.h"
#include "esp_timer.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "scan_mode";

#define MAX_DEVICES      10
#define NAME_LEN         18   // 18 chars × 8px = 144px
#define ROW_H            10
#define LIST_Y           20
#define MAX_VISIBLE      ((ST7789_HEIGHT - LIST_Y) / ROW_H)
#define RESCAN_US        15000000   // rescan 15s after discovery stops

// Last row is always "<< BACK". Real devices occupy rows 0..device_count-1.
#define TOTAL_ROWS       (device_count + 1)
#define BACK_ROW         (device_count)

typedef struct {
    uint8_t bda[6];
    char    name[NAME_LEN + 1];
} bt_device_t;

static bt_device_t devices[MAX_DEVICES];
static int         device_count   = 0;
static int         selected       = 0;    // 0 = Back, 1..N = devices
static int         discovery_done = 0;
static int64_t     scan_done_us   = 0;
static uint8_t     selected_bda[6];
static char        selected_name[NAME_LEN + 1];

// ---- Drawing ------------------------------------------------------------

static void draw_header(void)
{
    const char *label = discovery_done ? "SELECT DEVICE         "
                                       : "SCANNING...           ";
    st7789_draw_string(8, 4, label, COLOR_WHITE, COLOR_BLACK);
}

static void draw_row(int row, int highlight)
{
    int y = LIST_Y + row * ROW_H;
    uint16_t fg = highlight ? COLOR_BLACK : COLOR_WHITE;
    uint16_t bg = highlight ? COLOR_WHITE : COLOR_BLACK;

    char line[22];
    if (row == BACK_ROW)
        snprintf(line, sizeof(line), "%-*s", NAME_LEN + 2, "<< BACK");
    else
        snprintf(line, sizeof(line), "%-*s", NAME_LEN + 2, devices[row].name);

    st7789_draw_string(8, y, line, fg, bg);
}

static void draw_list(void)
{
    st7789_fill_rect(0, LIST_Y, ST7789_WIDTH, ST7789_HEIGHT - LIST_Y, COLOR_BLACK);
    int rows = TOTAL_ROWS < MAX_VISIBLE ? TOTAL_ROWS : MAX_VISIBLE;
    for (int i = 0; i < rows; i++)
        draw_row(i, i == selected);
    if (device_count == 0 && discovery_done)
        st7789_draw_string(8, LIST_Y, "NO DEVICES FOUND  ", COLOR_WHITE, COLOR_BLACK);
}

// ---- GAP callback -------------------------------------------------------

// CoD major device class sits at bits 12-8. 0x04 = Audio/Video.
#define COD_MAJOR_MASK      0x1F00
#define COD_MAJOR_AV        0x0400

static void on_disc_result(esp_bt_gap_cb_param_t *param)
{
    if (device_count >= MAX_DEVICES) return;

    // Filter to Audio/Video devices only
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
        if (p->type == ESP_BT_GAP_DEV_PROP_COD) {
            uint32_t cod = *(uint32_t *)p->val;
            if ((cod & COD_MAJOR_MASK) != COD_MAJOR_AV) return;
            break;
        }
    }

    for (int i = 0; i < device_count; i++) {
        if (memcmp(devices[i].bda, param->disc_res.bda, 6) == 0) return;
    }

    bt_device_t *dev = &devices[device_count];
    memcpy(dev->bda, param->disc_res.bda, 6);
    dev->name[0] = '\0';

    for (int i = 0; i < param->disc_res.num_prop; i++) {
        esp_bt_gap_dev_prop_t *p = &param->disc_res.prop[i];
        if (p->type == ESP_BT_GAP_DEV_PROP_BDNAME && p->len > 0) {
            int n = p->len < NAME_LEN ? p->len : NAME_LEN;
            memcpy(dev->name, p->val, n);
            dev->name[n] = '\0';
            break;
        }
        if (p->type == ESP_BT_GAP_DEV_PROP_EIR) {
            uint8_t len = 0;
            uint8_t *name = esp_bt_gap_resolve_eir_data(p->val,
                                ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
            if (!name)
                name = esp_bt_gap_resolve_eir_data(p->val,
                                ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
            if (name && len > 0) {
                int n = len < NAME_LEN ? len : NAME_LEN;
                memcpy(dev->name, name, n);
                dev->name[n] = '\0';
                break;
            }
        }
    }

    if (dev->name[0] == '\0') {
        uint8_t *b = dev->bda;
        snprintf(dev->name, sizeof(dev->name), "%02X:%02X:%02X:%02X:%02X:%02X",
                 b[0], b[1], b[2], b[3], b[4], b[5]);
    }

    ESP_LOGI(TAG, "found: %s", dev->name);
    device_count++;
    draw_list();
}

static void gap_scan_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT:
        on_disc_result(param);
        break;
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT:
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            discovery_done = 1;
            scan_done_us   = esp_timer_get_time();
            draw_header();
        }
        break;
    default:
        break;
    }
}

// ---- Public API ---------------------------------------------------------

void scan_mode_init(void)
{
    esp_bt_gap_register_callback(gap_scan_cb);
}

void scan_mode_enter(void)
{
    device_count   = 0;
    selected       = 0;
    discovery_done = 0;
    scan_done_us   = 0;

    st7789_fill(COLOR_BLACK);
    draw_header();
    draw_list();

    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0);
}

int scan_mode_tick(int encoder_delta, int button_pressed)
{
    static int last_count = -1;

    // Periodic rescan — keep the list fresh
    if (discovery_done && scan_done_us > 0
            && esp_timer_get_time() - scan_done_us >= RESCAN_US) {
        discovery_done = 0;
        scan_done_us   = 0;
        draw_header();
        esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 8, 0);
    }

    // Scroll
    if (encoder_delta != 0) {
        int prev = selected;
        selected += encoder_delta;
        if (selected < 0)              selected = 0;
        if (selected >= TOTAL_ROWS)    selected = TOTAL_ROWS - 1;
        if (selected != prev) {
            draw_row(prev, 0);
            draw_row(selected, 1);
        }
    }

    // Refresh if new devices appeared
    if (device_count != last_count) {
        draw_list();
        last_count = device_count;
    }

    if (button_pressed) {
        if (selected == BACK_ROW) {
            esp_bt_gap_cancel_discovery();
            return -1;
        }
        if (device_count > 0) {
            esp_bt_gap_cancel_discovery();
            memcpy(selected_bda, devices[selected].bda, 6);
            strncpy(selected_name, devices[selected].name, NAME_LEN);
            selected_name[NAME_LEN] = '\0';
            return 1;
        }
    }

    return 0;
}

uint8_t *scan_mode_get_bda(void)
{
    return selected_bda;
}

const char *scan_mode_get_name(void)
{
    return selected_name;
}

void scan_mode_set_name(const char *name)
{
    strncpy(selected_name, name ? name : "", NAME_LEN);
    selected_name[NAME_LEN] = '\0';
}
