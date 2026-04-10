#include "spi_audio_slave.h"
#include "spi_audio.h"
#include "bt_audio.h"

#include "driver/spi_slave.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "spi_slave";

#define SPI_SLAVE_HOST  SPI2_HOST
#define PIN_MISO        19
#define PIN_MOSI        23
#define PIN_SCLK        18
#define PIN_CS           5
#define PIN_HANDSHAKE    4   // asserted HIGH when a transaction is queued and ready

// Called by SPI slave driver just after CS is asserted by the master.
// Deassert handshake — master has started the transaction.
static void IRAM_ATTR post_setup_cb(spi_slave_transaction_t *trans)
{
    gpio_set_level(PIN_HANDSHAKE, 0);
}

// Called by SPI slave driver after the transaction completes.
// Reassert handshake once we've refilled the buffer and re-queued.
// (Actual re-queue happens in the task loop, not here.)
static void IRAM_ATTR post_trans_cb(spi_slave_transaction_t *trans)
{
    // nothing — task loop asserts handshake after filling the next frame
}

static void spi_slave_task(void *arg)
{
    static WORD_ALIGNED_ATTR spi_audio_frame_t tx_frame;
    static WORD_ALIGNED_ATTR uint8_t rx_dummy[sizeof(spi_audio_frame_t)];
    uint16_t seq = 0;

    spi_slave_transaction_t t = {
        .length    = sizeof(spi_audio_frame_t) * 8,
        .tx_buffer = &tx_frame,
        .rx_buffer = rx_dummy,
    };

    while (1) {
        // Fill frame
        tx_frame.magic      = SPI_AUDIO_MAGIC;
        tx_frame.volume     = (uint8_t)bt_audio_get_volume();
        tx_frame.conn_state = (uint8_t)bt_audio_get_state();
        tx_frame.seq        = seq++;
        bt_audio_copy_frame(tx_frame.pcm, SPI_AUDIO_FRAME_SAMPLES);

        // Log first few frames so we can verify layout on the wire
        static int log_count = 0;
        if (log_count < 5) {
            uint8_t *b = (uint8_t *)&tx_frame;
            ESP_LOGI(TAG, "tx bytes[0..7]: %02X %02X %02X %02X %02X %02X %02X %02X",
                     b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
            log_count++;
        }

        // Arm the hardware FIRST, then signal — prevents master firing before slave is ready
        spi_slave_queue_trans(SPI_SLAVE_HOST, &t, portMAX_DELAY);
        gpio_set_level(PIN_HANDSHAKE, 1);

        // Wait for master to clock it out
        spi_slave_transaction_t *ret;
        spi_slave_get_trans_result(SPI_SLAVE_HOST, &ret, portMAX_DELAY);
    }
}

void spi_audio_slave_init(void)
{
    // Handshake pin — output, starts LOW
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_HANDSHAKE,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(PIN_HANDSHAKE, 0);

    spi_bus_config_t bus = {
        .mosi_io_num   = PIN_MOSI,
        .miso_io_num   = PIN_MISO,
        .sclk_io_num   = PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    spi_slave_interface_config_t slave_cfg = {
        .mode          = 0,
        .spics_io_num  = PIN_CS,
        .queue_size    = 1,
        .flags         = 0,
        .post_setup_cb = post_setup_cb,
        .post_trans_cb = post_trans_cb,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(SPI_SLAVE_HOST, &bus, &slave_cfg, SPI_DMA_CH_AUTO));
    ESP_LOGI(TAG, "ready on SPI2, CS=GPIO%d, handshake=GPIO%d", PIN_CS, PIN_HANDSHAKE);

    xTaskCreate(spi_slave_task, "spi_slave", 4096, NULL, 10, NULL);
}
