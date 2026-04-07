#pragma once

// KY-040 rotary encoder on GPIO33 (CLK), GPIO32 (DT), GPIO27 (SW)
void encoder_init(void);

// Returns accumulated rotation delta since last call, then resets to zero.
// Positive = clockwise, negative = counter-clockwise.
int encoder_get_delta(void);

// Returns 1 if the push button is currently held down, 0 otherwise.
int encoder_button_pressed(void);
