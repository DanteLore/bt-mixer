#pragma once

// KY-040 rotary encoder, quadrature state machine, ISR-driven.
// Call encoder_init() with the GPIO numbers for CLK, DT, and SW.
void encoder_init(int pin_clk, int pin_dt, int pin_sw);

// Returns accumulated rotation delta since last call, then resets to zero.
// Positive = clockwise, negative = counter-clockwise.
int encoder_get_delta(void);

// Returns 1 if the push button is currently held down, 0 otherwise.
int encoder_button_pressed(void);
