#include "uart_audio_slave.h"
#include "spi_audio.h"
#include "bt_audio.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"

#include <string.h>
#include <stddef.h>

static const char *TAG = "uart_slave";

#define UART_PORT    UART_NUM_2
#define PIN_TX       17
#define PIN_CTS      4    // ← master GPIO25 (RTS)
#define UART_BAUD    2000000
#define ENC_BUF_SIZE (sizeof(spi_audio_frame_t) + sizeof(spi_audio_frame_t)/254 + 2)
#define TX_BUF_SIZE  16384

// Queue between the BT audio callback (producer) and the UART TX task (consumer).
// The BT callback must never block (it runs on the BT stack task), so frames are
// enqueued non-blocking and the TX task owns all uart_write_bytes calls.
// When the master asserts RTS, uart_write_bytes blocks until CTS is released;
// TX_QUEUE_DEPTH must be large enough to absorb that holdoff without dropping frames.
// At 344 frames/s, a 90ms CTS holdoff fills ~31 queue slots — hence depth 32.
typedef struct {
    int16_t pcm[SPI_AUDIO_FRAME_SAMPLES * 2];
    uint8_t volume;
    uint8_t conn_state;
} tx_item_t;

#define TX_QUEUE_DEPTH  32
static QueueHandle_t tx_queue;

static uint16_t seq        = 0;
static int dbg_sent        = 0;
static int dbg_dropped     = 0;  // frames lost because queue was full
static int64_t dbg_last_us = 0;

// COBS encode src (len bytes) into dst, appending a 0x00 frame delimiter.
// dst must be at least len + len/254 + 2 bytes.
// Returns total bytes written including the 0x00 delimiter.
static int cobs_encode(const uint8_t *src, size_t len, uint8_t *dst)
{
    size_t di = 0;
    size_t code_pos = di++;
    uint8_t code = 1;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == 0) {
            dst[code_pos] = code;
            code_pos = di++;
            code = 1;
        } else {
            dst[di++] = src[i];
            if (++code == 255) {
                dst[code_pos] = code;
                code_pos = di++;
                code = 1;
            }
        }
    }
    dst[code_pos] = code;
    dst[di++] = 0x00;
    return (int)di;
}

static void send_pcm(const int16_t *pcm, uint8_t volume, uint8_t conn_state,
                     spi_audio_frame_t *frame, uint8_t *enc_buf)
{
    frame->magic      = SPI_AUDIO_MAGIC;
    frame->volume     = volume;
    frame->conn_state = conn_state;
    frame->seq        = seq++;
    memcpy(frame->pcm, pcm, sizeof(frame->pcm));
    frame->crc = esp_rom_crc32_le(0, (const uint8_t *)frame,
                                  offsetof(spi_audio_frame_t, crc));
    int enc_len = cobs_encode((const uint8_t *)frame, sizeof(*frame), enc_buf);
    int written = uart_write_bytes(UART_PORT, enc_buf, enc_len);
    if (written == enc_len)
        dbg_sent++;
    else
        ESP_LOGW(TAG, "partial write: %d/%d — TX buffer full", written, enc_len);
}

static void uart_tx_task(void *arg)
{
    static tx_item_t item;
    static spi_audio_frame_t frame;
    static uint8_t enc_buf[ENC_BUF_SIZE];

    while (1) {
        xQueueReceive(tx_queue, &item, portMAX_DELAY);
        send_pcm(item.pcm, item.volume, item.conn_state, &frame, enc_buf);

        int64_t t = esp_timer_get_time();
        if (dbg_last_us == 0) dbg_last_us = t;
        if (t - dbg_last_us >= 5000000) {
            ESP_LOGI(TAG, "sent=%d  dropped=%d  seq=%u",
                     dbg_sent, dbg_dropped, (unsigned)seq);
            dbg_sent = dbg_dropped = 0;
            dbg_last_us = t;
        }
    }
}

// Called from the BT audio task — must return quickly, no blocking allowed.
static void on_frame_ready(void)
{
    tx_item_t item;
    bt_audio_copy_frame(item.pcm, SPI_AUDIO_FRAME_SAMPLES);
    item.volume     = (uint8_t)bt_audio_get_volume();
    item.conn_state = (uint8_t)bt_audio_get_state();
    if (xQueueSend(tx_queue, &item, 0) != pdTRUE)
        dbg_dropped++;
}

void uart_audio_slave_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_CTS,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, PIN_TX, UART_PIN_NO_CHANGE,
                                 UART_PIN_NO_CHANGE, PIN_CTS));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, 256, TX_BUF_SIZE, 0, NULL, 0));

    tx_queue = xQueueCreate(TX_QUEUE_DEPTH, sizeof(tx_item_t));
    // Core 1: BT stack runs on core 0; putting a tight polling task there triggers the WDT
    xTaskCreatePinnedToCore(uart_tx_task, "uart_tx", 4096, NULL, 5, NULL, 1);

    bt_audio_set_frame_callback(on_frame_ready);

    ESP_LOGI(TAG, "ready on UART2 TX=GPIO%d at %d baud", PIN_TX, UART_BAUD);
}
