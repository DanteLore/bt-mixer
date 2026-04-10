#include "uart_audio_master.h"
#include "spi_audio.h"

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_crc.h"

#include <string.h>
#include <stddef.h>
#include <stdbool.h>

static const char *TAG = "uart_master";

#define UART_PORT    UART_NUM_2
#define PIN_RX       16
#define PIN_RTS      25   // → channel GPIO4 (CTS)
#define UART_BAUD    2000000
#define RX_BUF_SIZE  32768

static QueueHandle_t uart_queue;

// Encoded frame is at most sizeof(frame) + ceil(sizeof(frame)/254) + 1 bytes
#define ENC_BUF_SIZE (sizeof(spi_audio_frame_t) + sizeof(spi_audio_frame_t)/254 + 2)

#define RING_FRAMES  32
static spi_audio_frame_t rx_ring[RING_FRAMES];
static volatile int ring_write = 0;
static volatile int ring_read  = 0;
static int read_pos = 0;
static spi_audio_frame_t last_frame;

static volatile int dbg_new_frames  = 0;
static volatile int dbg_overruns    = 0;
static volatile int dbg_underruns   = 0;
static volatile int dbg_fifo_ovf    = 0;  // UART hardware FIFO overflow
static volatile int dbg_buf_full    = 0;  // UART software ring buffer full
static volatile int dbg_enc_overflow = 0; // enc_buf overflow → frame discarded, resyncing
static volatile int dbg_bad_size    = 0;  // COBS decoded wrong length
static volatile int dbg_bad_magic   = 0;  // magic field mismatch
static volatile int dbg_bad_crc     = 0;  // CRC mismatch
static volatile int dbg_seq_gaps    = 0;
static int64_t      dbg_last_us    = 0;
static uint16_t     last_rx_seq    = 0;
static bool         have_seq       = false;

// COBS decode src (len bytes, NOT including the 0x00 delimiter) into dst.
// Returns decoded length, or -1 on error.
static int cobs_decode(const uint8_t *src, int len, uint8_t *dst)
{
    int di = 0, i = 0;
    while (i < len) {
        uint8_t code = src[i++];
        if (code == 0) return -1;
        int copy = code - 1;
        if (i + copy > len) return -1;
        memcpy(dst + di, src + i, copy);
        di += copy;
        i  += copy;
        if (code < 255 && i < len)
            dst[di++] = 0;
    }
    return di;
}

static void uart_rx_task(void *arg)
{
    static uint8_t enc_buf[ENC_BUF_SIZE];
    static uint8_t chunk[256];
    static spi_audio_frame_t frame;
    int enc_len = 0;

    while (1) {
        // Block until at least 1 byte arrives — lets IDLE task run when quiet
        int got = uart_read_bytes(UART_PORT, chunk, sizeof(chunk), portMAX_DELAY);
        uart_event_t evt;
        while (xQueueReceive(uart_queue, &evt, 0)) {
            if      (evt.type == UART_FIFO_OVF)    dbg_fifo_ovf++;
            else if (evt.type == UART_BUFFER_FULL)  dbg_buf_full++;
        }
        for (int i = 0; i < got; i++) {
            uint8_t b = chunk[i];
            if (b != 0x00) {
                if (enc_len < (int)ENC_BUF_SIZE)
                    enc_buf[enc_len++] = b;
                else {
                    enc_len = 0;  // overlong — discard and resync
                    dbg_enc_overflow++;
                }
                continue;
            }

            // 0x00 = end of COBS frame
            if (enc_len == 0) continue;

            int dec_len = cobs_decode(enc_buf, enc_len, (uint8_t *)&frame);
            enc_len = 0;

            if (dec_len != (int)sizeof(spi_audio_frame_t)) { dbg_bad_size++;  continue; }
            if (frame.magic != SPI_AUDIO_MAGIC)             { dbg_bad_magic++; continue; }

            uint32_t computed = esp_rom_crc32_le(0, (const uint8_t *)&frame,
                                                 offsetof(spi_audio_frame_t, crc));
            if (computed != frame.crc) { dbg_bad_crc++; continue; }

            if (have_seq)
                dbg_seq_gaps += (uint16_t)(frame.seq - (last_rx_seq + 1));
            last_rx_seq = frame.seq;
            have_seq    = true;

            int next = (ring_write + 1) % RING_FRAMES;
            if (next == ring_read) {
                dbg_overruns++;
            } else {
                memcpy(&rx_ring[ring_write], &frame, sizeof(frame));
                ring_write = next;
                dbg_new_frames++;
            }
        }
    }
}

void uart_audio_master_init(int n_channels)
{
    (void)n_channels;

    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_RTS,
        .rx_flow_ctrl_thresh = 120, // assert RTS when FIFO has ≥120 bytes (FIFO=128; leaves margin for in-flight bytes)
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_PIN_NO_CHANGE, PIN_RX,
                                 PIN_RTS, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, RX_BUF_SIZE, 0, 20, &uart_queue, 0));

    dbg_last_us = esp_timer_get_time();
    // Priority 10 — needs to drain RX promptly to release CTS; yields when blocked on portMAX_DELAY
    xTaskCreatePinnedToCore(uart_rx_task, "uart_rx", 4096, NULL, 10, NULL, 1);
    ESP_LOGI(TAG, "ready on UART2 RX=GPIO%d at %d baud", PIN_RX, UART_BAUD);
}

void uart_audio_master_get_samples(int16_t *dst, int n_stereo_samples)
{
    for (int i = 0; i < n_stereo_samples; i++) {
        if (ring_read == ring_write) {
            dst[i * 2]     = last_frame.pcm[read_pos * 2];
            dst[i * 2 + 1] = last_frame.pcm[read_pos * 2 + 1];
            if (++read_pos >= SPI_AUDIO_FRAME_SAMPLES) read_pos = 0;
            dbg_underruns++;
        } else {
            dst[i * 2]     = rx_ring[ring_read].pcm[read_pos * 2];
            dst[i * 2 + 1] = rx_ring[ring_read].pcm[read_pos * 2 + 1];
            if (++read_pos >= SPI_AUDIO_FRAME_SAMPLES) {
                memcpy(&last_frame, &rx_ring[ring_read], sizeof(last_frame));
                read_pos  = 0;
                ring_read = (ring_read + 1) % RING_FRAMES;
            }
        }
    }

    int64_t now = esp_timer_get_time();
    if (now - dbg_last_us >= 5000000) {
        int fill = (ring_write - ring_read + RING_FRAMES) % RING_FRAMES;
        ESP_LOGI(TAG, "ring=%d/%d  new=%d  fifo_ovf=%d  buf_full=%d  enc_ovf=%d  bad(size=%d magic=%d crc=%d)  gaps=%d  underrun=%d  overrun=%d",
                 fill, RING_FRAMES, dbg_new_frames,
                 dbg_fifo_ovf, dbg_buf_full, dbg_enc_overflow,
                 dbg_bad_size, dbg_bad_magic, dbg_bad_crc,
                 dbg_seq_gaps, dbg_underruns, dbg_overruns);
        dbg_new_frames = dbg_underruns = dbg_overruns =
            dbg_fifo_ovf = dbg_buf_full = dbg_enc_overflow =
            dbg_bad_size = dbg_bad_magic = dbg_bad_crc = dbg_seq_gaps = 0;
        dbg_last_us = now;
    }
}
