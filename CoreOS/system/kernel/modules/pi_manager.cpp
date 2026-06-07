#include "pi_manager.h"
#include "../purr_idf_compat.h"

// Forward declarations to kitt.cpp — pi_manager calls back into KITT
// to trigger display yield/reclaim without a circular include.
extern void kitt_display_yield_to_pi();
extern void kitt_display_reclaim_from_pi();

static pi_state_t pi_state      = PI_STATE_ABSENT;
static pi_state_t last_pi_state = PI_STATE_ABSENT;
static bool       gate_on       = false;

void pi_manager_init() {
    pinMode(PI_GATE_PIN,      OUTPUT);
    pinMode(PI_HANDSHAKE_PIN, INPUT_PULLDOWN);
    digitalWrite(PI_GATE_PIN, LOW);
    gate_on = false;
    pi_manager_update();
    Serial.println("[pi] manager init OK");
}

void pi_manager_update() {
    bool gate      = digitalRead(PI_GATE_PIN);
    bool handshake = digitalRead(PI_HANDSHAKE_PIN);

    if (!handshake && !gate) pi_state = PI_STATE_ABSENT;
    else if (!handshake &&  gate) pi_state = PI_STATE_POWERING_UP;
    else if ( handshake &&  gate) pi_state = PI_STATE_ACTIVE;
    else                          pi_state = PI_STATE_ANOMALY;

    if (pi_state == PI_STATE_ACTIVE && last_pi_state != PI_STATE_ACTIVE)
        kitt_display_yield_to_pi();

    if (pi_state == PI_STATE_ABSENT && last_pi_state == PI_STATE_ACTIVE)
        kitt_display_reclaim_from_pi();

    last_pi_state = pi_state;
}

void pi_manager_deinit() {
    if (gate_on) pi_manager_power_off();
}

void pi_manager_power_on() {
    digitalWrite(PI_GATE_PIN, HIGH);
    gate_on = true;
    Serial.println("[pi] rail enabled, waiting for handshake...");

    uint32_t start = millis();
    while (!digitalRead(PI_HANDSHAKE_PIN) && millis() - start < 30000)
        delay(100);

    if (digitalRead(PI_HANDSHAKE_PIN))
        Serial.println("[pi] CM5 active");
    else
        Serial.println("[pi] CM5 handshake timeout");
}

void pi_manager_power_off() {
    Serial1.begin(115200, SERIAL_8N1, PI_UART_RX, PI_UART_TX);
    Serial1.println("HALT");
    Serial.println("[pi] halt sent, waiting for CM5 shutdown...");

    uint32_t start = millis();
    while (digitalRead(PI_HANDSHAKE_PIN) && millis() - start < 10000)
        delay(100);

    digitalWrite(PI_GATE_PIN, LOW);
    gate_on = false;
    Serial.println("[pi] rail disabled");
}

pi_state_t pi_manager_state()          { return pi_state; }
bool       pi_manager_handshake_high() { return digitalRead(PI_HANDSHAKE_PIN); }
bool       pi_manager_rail_enabled()   { return gate_on; }
