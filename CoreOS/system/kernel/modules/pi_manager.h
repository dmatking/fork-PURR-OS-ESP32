#pragma once
#include <stdbool.h>

// CattoPad only — CM5 power gate + display handoff
// From board.md: Pi rail is on P-MOSFET. Handshake = CM5 GPIO pin.
// Adjust pin numbers to match final board.md signal assignments.
#define PI_GATE_PIN      40
#define PI_HANDSHAKE_PIN 41
#define PI_UART_TX       43
#define PI_UART_RX       44

typedef enum {
    PI_STATE_ABSENT,
    PI_STATE_POWERING_UP,
    PI_STATE_ACTIVE,
    PI_STATE_ANOMALY
} pi_state_t;

void        pi_manager_init();
void        pi_manager_update();
void        pi_manager_deinit();
void        pi_manager_power_on();
void        pi_manager_power_off();
pi_state_t  pi_manager_state();
bool        pi_manager_handshake_high();
bool        pi_manager_rail_enabled();
