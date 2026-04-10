#include "spi_audio_master.h"
#include "spi_audio.h"

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>

static const char *TAG = "spi_master";

#define SPI_MASTER_HOST  SPI3_HOST
#define PIN_MISO     19
#define PIN_MOSI     22
#define PIN_SCLK     21
#define SPI_FREQ_HZ  (10 * 1000 * 1000)

#define SPI_AUDIO_MAX_CHANNELS  3
static const int cs_pins[SPI_AUDIO_MAX_CHANNELS]        = {25, 26, 13};
static const int handshake_pins[SPI_AUDIO_MAX_CHANNELS] = {16, 17, -1};  // GPIO16/17 = free UART2 RX/TX pins

#define RING_FRAMES  8
static WORD_ALIGNED_ATTR spi_audio_frame_t rx_ring[RING_FRAMES];
static volatile int ring_write = 0;
static volatile int ring_read  = 0;
static int read_pos = 0;

static spi_device_handle_t ch_devices[SPI_AUDIO_MAX_CHANNELS];
static int num_channels = 0;
static SemaphoreHandle_t spi_mutex;

static uint16_t last_seq[SPI_AUDIO_MAX_CHANNELS];

// Debug counters — reported every 5 seconds
static volatile int dbg_underruns  = 0;
static volatile int dbg_overruns   = 0;
static volatile int dbg_new_frames = 0;
static volatile int dbg_get_calls  = 0;
static int64_t      dbg_last_us    = 0;
static spi_audio_frame_t dbg_last_frame;

static bool handshake_ready(int ch)
{
    if (handshake_pins[ch] < 0) return true;
    return gpio_get_level(handshake_pins[ch]) == 1;
}

static bool fetch_channel(int ch)
{
    static WORD_ALIGNED_ATTR spi_audio_frame_t tmp;

    if (!handshake_ready(ch)) return false;
    if (xSemaphoreTake(spi_mutex, 0) != pdTRUE) return false;

    spi_transaction_t t = {
        .length    = sizeof(spi_audio_frame_t) * 8,
        .rxlength  = sizeof(spi_audio_frame_t) * 8,
        .rx_buffer = &tmp,
    };
    spi_device_transmit(ch_devices[ch], &t);
    xSemaphoreGive(spi_mutex);

    // DMA MISO launch latency prepends 1 zero bit — right-shift the whole buffer by 1
    {
        uint8_t *b = (uint8_t *)&tmp;
        const size_t n = sizeof(spi_audio_frame_t);
        for (size_t i = n - 1; i > 0; i--)
            b[i] = (uint8_t)((b[i - 1] << 7) | (b[i] >> 1));
        b[0] >>= 1;
    }

    static int raw_logged = 0;
    if (!raw_logged) {
        uint8_t *b = (uint8_t *)&tmp;
        ESP_LOGI(TAG, "corrected rx bytes[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X",
                 b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        ESP_LOGI(TAG, "magic as uint32: 0x%08X (expected 0x%08X)", (unsigned)tmp.magic, SPI_AUDIO_MAGIC);
        raw_logged = 1;
    }

    if (tmp.magic != SPI_AUDIO_MAGIC) {
        ESP_LOGW(TAG, "magic mismatch: 0x%08X", (unsigned)tmp.magic);
        return true;  // slave was drained; don't write garbage into ring
    }

    memcpy(&dbg_last_frame, &tmp, sizeof(tmp));

    int next = (ring_write + 1) % RING_FRAMES;
    if (next == ring_read) {
        // Ring full — frame discarded but slave was still drained, no deadlock
        dbg_overruns++;
        return true;
    }
    // TODO: mix multiple channels here when num_channels > 1
    memcpy(&rx_ring[ring_write], &tmp, sizeof(tmp));
    last_seq[ch] = tmp.seq;
    ring_write = next;
    dbg_new_frames++;
    return true;
}

void spi_audio_master_init(int n_channels)
{
    num_channels = n_channels > SPI_AUDIO_MAX_CHANNELS ? SPI_AUDIO_MAX_CHANNELS : n_channels;
    memset(last_seq, 0xFF, sizeof(last_seq));

    spi_bus_config_t bus = {
        .mosi_io_num   = PIN_MOSI,
        .miso_io_num   = PIN_MISO,
        .sclk_io_num   = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    // Configure handshake input pins
    for (int i = 0; i < num_channels; i++) {
        if (handshake_pins[i] < 0) continue;
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << handshake_pins[i],
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_ENABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);
    }

    ESP_ERROR_CHECK(spi_bus_initialize(SPI_MASTER_HOST, &bus, SPI_DMA_CH_AUTO));

    for (int i = 0; i < num_channels; i++) {
        spi_device_interface_config_t dev = {
            .clock_speed_hz = SPI_FREQ_HZ,
            .mode           = 0,
            .spics_io_num   = cs_pins[i],
            .queue_size     = 1,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(SPI_MASTER_HOST, &dev, &ch_devices[i]));
    }

    spi_mutex = xSemaphoreCreateMutex();
    dbg_last_us = esp_timer_get_time();
    ESP_LOGI(TAG, "%d channel(s) ready on SPI3", num_channels);
}

void spi_audio_master_get_samples(int16_t *dst, int n_stereo_samples)
{
    dbg_get_calls++;

    // Always try to fetch if handshake is ready — never leave the slave armed and waiting
    while ((ring_write + 1) % RING_FRAMES != ring_read) {
        if (!fetch_channel(0)) break;
    }

    for (int i = 0; i < n_stereo_samples; i++) {
        if (ring_read == ring_write) {
            dst[i * 2]     = 0;
            dst[i * 2 + 1] = 0;
            dbg_underruns++;
        } else {
            dst[i * 2]     = rx_ring[ring_read].pcm[read_pos * 2];
            dst[i * 2 + 1] = rx_ring[ring_read].pcm[read_pos * 2 + 1];
            if (++read_pos >= SPI_AUDIO_FRAME_SAMPLES) {
                read_pos = 0;
                ring_read = (ring_read + 1) % RING_FRAMES;
            }
        }
    }

    // Report stats every 5s
    int64_t now = esp_timer_get_time();
    if (now - dbg_last_us >= 5000000) {
        int fill = (ring_write - ring_read + RING_FRAMES) % RING_FRAMES;
        ESP_LOGI(TAG, "ring=%d/%d  new=%d  underrun=%d  overrun=%d  get_calls=%d  ch0_vol=%d  ch0_conn=%d",
                 fill, RING_FRAMES,
                 dbg_new_frames, dbg_underruns, dbg_overruns, dbg_get_calls,
                 dbg_last_frame.volume, dbg_last_frame.conn_state);
        dbg_underruns = dbg_overruns = dbg_new_frames = dbg_get_calls = 0;
        dbg_last_us = now;
    }
}
