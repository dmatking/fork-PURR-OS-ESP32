#include "power_manager.h"
#include <Arduino.h>
#include <esp_pm.h>

static int cached_percent   = 0;
static int cached_voltage   = 0;

// Map raw ADC millivolts (after voltage divider) to battery percent.
// Typical LiPo: 4200mV = 100%, 3000mV = 0%.
static int voltage_to_percent(int mv) {
    if (mv >= 4200) return 100;
    if (mv <= 3000) return 0;
    return (mv - 3000) * 100 / 1200;
}

void power_manager_init(uint16_t cpu_max_mhz) {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);

    if (BATT_CHG_PIN > 0)
        pinMode(BATT_CHG_PIN, INPUT);

    power_manager_cpu_set_freq((int)cpu_max_mhz);
    power_manager_refresh_battery();

    Serial.printf("[pwr] init OK cpu=%dMHz batt=%d%%\n",
                  power_manager_cpu_get_freq(), cached_percent);
}

void power_manager_update() {}

void power_manager_deinit() {}

void power_manager_refresh_battery() {
    // Average 8 samples to reduce ADC noise
    int sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BATT_ADC_PIN);
    int adc_mv = sum / 8;

    // Voltage divider correction — adjust R1/R2 ratio per schematic
    // Placeholder: 1:1 divider (no resistor divide on Heltec V3 — direct ADC)
    cached_voltage = adc_mv;
    cached_percent = voltage_to_percent(cached_voltage);
}

int  power_manager_battery_percent()    { return cached_percent; }
int  power_manager_battery_voltage_mv() { return cached_voltage; }
int  power_manager_battery_current_ma() { return 0; }  // no current sense on target hw
bool power_manager_battery_charging() {
    return (BATT_CHG_PIN > 0) && digitalRead(BATT_CHG_PIN);
}

void power_manager_cpu_set_freq(int mhz) {
    if (mhz != 80 && mhz != 160 && mhz != 240) mhz = 240;
    setCpuFrequencyMhz(mhz);
}

int power_manager_cpu_get_freq() {
    return (int)getCpuFrequencyMhz();
}
