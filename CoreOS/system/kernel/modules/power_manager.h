#pragma once
#include <stdint.h>
#include <stdbool.h>

// Battery ADC — set to -1 on devices with no battery circuit
#if defined(PURR_DISPLAY_ILI9341)
#  define BATT_ADC_PIN  -1   // CYD: USB-powered, no battery divider
#  define BATT_CHG_PIN  -1
#else
#  define BATT_ADC_PIN   1   // Heltec V3: ADC1_CH0 (GPIO 1)
#  define BATT_CHG_PIN  34   // HIGH = charging
#endif

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
