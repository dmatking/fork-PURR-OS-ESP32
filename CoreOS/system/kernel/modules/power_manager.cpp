// power_manager.cpp — power and CPU management (pure ESP-IDF)

#include "power_manager.h"
#include "../purr_idf_compat.h"
#include "esp_pm.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_clk_tree.h"
#include "soc/rtc.h"
#include <stdint.h>

#if BATT_ADC_PIN >= 0
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#endif

static const char* TAG = "pwr";
static int cached_percent   = -1;
static int cached_voltage   = 0;
static int cached_cpu_freq  = 240;

#if BATT_ADC_PIN >= 0
static adc_oneshot_unit_handle_t s_adc  = NULL;
static adc_cali_handle_t         s_cali = NULL;
static int                       s_chan  = -1;

static void adc_init() {
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&unit_cfg, &s_adc);

    // Map GPIO → ADC channel (ESP32 ADC1 channels)
    adc_channel_t chan;
    adc_oneshot_io_to_channel(BATT_ADC_PIN, NULL, &chan);
    s_chan = (int)chan;

    adc_oneshot_chan_cfg_t ch_cfg = {
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(s_adc, chan, &ch_cfg);

    // Calibration (curve-fitting — required on ESP32-S3; line-fitting is ESP32 only)
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = (adc_channel_t)s_chan,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
}
#endif

__attribute__((unused))
static int voltage_to_percent(int mv) {
    if (mv >= 4200) return 100;
    if (mv <= 3000) return 0;
    return (mv - 3000) * 100 / 1200;
}

void power_manager_init(uint16_t cpu_max_mhz) {
#if BATT_ADC_PIN >= 0
    adc_init();
#endif
#if BATT_CHG_PIN >= 0
    gpio_set_direction((gpio_num_t)BATT_CHG_PIN, GPIO_MODE_INPUT);
#endif
    power_manager_cpu_set_freq((int)cpu_max_mhz);
    power_manager_refresh_battery();
    ESP_LOGI(TAG, "init OK cpu=%dMHz batt=%d%%",
             power_manager_cpu_get_freq(), cached_percent);
}

void power_manager_update()  {}
void power_manager_deinit()  {}

void power_manager_refresh_battery() {
#if BATT_ADC_PIN >= 0
    if (!s_adc) return;
    int sum_mv = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        adc_oneshot_read(s_adc, (adc_channel_t)s_chan, &raw);
        int mv = 0;
        if (s_cali) adc_cali_raw_to_voltage(s_cali, raw, &mv);
        else mv = raw * 3300 / 4095;
        sum_mv += mv;
    }
    cached_voltage = sum_mv / 8;
    cached_percent = voltage_to_percent(cached_voltage);
#else
    cached_voltage = 0;
    cached_percent = -1;
#endif
}

int  power_manager_battery_percent()    { return cached_percent; }
int  power_manager_battery_voltage_mv() { return cached_voltage; }
int  power_manager_battery_current_ma() { return 0; }
bool power_manager_battery_charging() {
#if BATT_CHG_PIN >= 0
    return gpio_get_level((gpio_num_t)BATT_CHG_PIN) == HIGH;
#else
    return false;
#endif
}

void power_manager_cpu_set_freq(int mhz) {
    if (mhz != 80 && mhz != 160 && mhz != 240) mhz = 240;
    cached_cpu_freq = mhz;
    esp_pm_config_t pm_cfg = {
        .max_freq_mhz = mhz,
        .min_freq_mhz = 80,
        .light_sleep_enable = false,
    };
    esp_pm_configure(&pm_cfg);
}

int power_manager_cpu_get_freq() {
    return cached_cpu_freq;
}
