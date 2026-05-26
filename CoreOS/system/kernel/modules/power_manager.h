#pragma once
#include <stdint.h>
#include <stdbool.h>

// ADC1 pin for battery voltage divider (ADC2 unusable during WiFi)
#define BATT_ADC_PIN   1
#define BATT_CHG_PIN  34    // HIGH = charging (adjust per schematic)

void power_manager_init(uint16_t cpu_max_mhz);
void power_manager_update();
void power_manager_deinit();
void power_manager_refresh_battery();

int  power_manager_battery_percent();
int  power_manager_battery_voltage_mv();
int  power_manager_battery_current_ma();
bool power_manager_battery_charging();
void power_manager_cpu_set_freq(int mhz);
int  power_manager_cpu_get_freq();
