#include "encoder.h"
#include "driver/gpio.h"
#include "esp_attr.h"
#include <stdatomic.h>

// CPU runs at 240MHz — cycles per millisecond
#define CYCLES_PER_MS  240000

static inline uint32_t IRAM_ATTR get_ccount(void) {
    uint32_t c;
    __asm__ __volatile__("rsr %0, ccount" : "=a"(c));
    return c;
}

static int s_pin_clk, s_pin_dt, s_pin_sw;

// Lookup table indexed by (prev_state << 2) | curr_state.
// State bits: CLK=bit1, DT=bit0. Returns +1, -1, or 0 for invalid/bounce.
static const int8_t enc_table[16] = {
     0,  1, -1,  0,
    -1,  0,  0,  1,
     1,  0,  0, -1,
     0, -1,  1,  0,
};

static atomic_int delta;
static volatile uint8_t  last_state;
static volatile int8_t   accumulated;
static volatile uint32_t last_detent_cycles;

static void IRAM_ATTR encoder_isr(void *arg)
{
    uint8_t curr = (gpio_get_level(s_pin_clk) << 1) | gpio_get_level(s_pin_dt);
    if (curr == last_state) return;

    int change = enc_table[(last_state << 2) | curr];
    if (change != 0) {
        accumulated += change;
        if (curr == 0b11) {
            int dir = (accumulated > 0) ? -1 : (accumulated < 0) ? 1 : 0;
            if (dir != 0) {
                // Scale step by how fast the knob is turning.
                // Fast = small gap between detents → bigger step.
                uint32_t now     = get_ccount();
                uint32_t elapsed = now - last_detent_cycles;  // wraps safely
                int step;
                if      (elapsed < 2  * CYCLES_PER_MS) step = 8;
                else if (elapsed < 5  * CYCLES_PER_MS) step = 4;
                else if (elapsed < 15 * CYCLES_PER_MS) step = 2;
                else                                    step = 1;
                atomic_fetch_add(&delta, dir * step);
                last_detent_cycles = now;
            }
            accumulated = 0;
        }
    }

    last_state = curr;
}

void encoder_init(int pin_clk, int pin_dt, int pin_sw)
{
    s_pin_clk = pin_clk;
    s_pin_dt  = pin_dt;
    s_pin_sw  = pin_sw;

    atomic_init(&delta, 0);
    accumulated = 0;
    last_detent_cycles = 0;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << pin_clk) | (1ULL << pin_dt) | (1ULL << pin_sw),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,   // KY-040 has onboard pull-ups
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    last_state = (gpio_get_level(pin_clk) << 1) | gpio_get_level(pin_dt);

    gpio_set_intr_type(pin_clk, GPIO_INTR_ANYEDGE);
    gpio_set_intr_type(pin_dt,  GPIO_INTR_ANYEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(pin_clk, encoder_isr, NULL);
    gpio_isr_handler_add(pin_dt,  encoder_isr, NULL);
}

int encoder_get_delta(void)
{
    return atomic_exchange(&delta, 0);
}

int encoder_button_pressed(void)
{
    return gpio_get_level(s_pin_sw) == 0;
}
