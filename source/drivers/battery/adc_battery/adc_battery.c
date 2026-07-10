// adc_battery.c — single-pin ADC battery voltage/percent driver.
//
// No fuel-gauge IC on this board — LilyGo's T-Deck/T-Deck Plus measure
// battery voltage the same simple way Meshtastic's own firmware does
// (confirmed against their variant.h): a resistive divider on GPIO4 into
// ADC1, ~2:1 ratio. Pushes into the same plain kernel-global pattern
// wifi_mgr/bt_mgr/mesh already use for sd_available/wifi_connected/
// lora_available — no catcall_battery_t contract exists (or is needed);
// nothing else in the kernel dispatches through a battery driver, it's
// purely push-based status.

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

#include "../../../kernel/core/purr_module.h"
#include "../../../kernel/core/purr_kernel.h"

static const char *TAG = "adc_battery";

// GPIO4 / ADC1_CHANNEL_3 — confirmed against Meshtastic's own T-Deck
// variant.h (`#define BATTERY_PIN 4`, `ADC1_GPIO4_CHANNEL`). Not
// configurable via device.pcat's [pins] section today — see bbq20.c's
// KB_BL_PIN comment for why (purr_device_glue.c's CONFIG_DRV_*_PIN
// #defines are scoped to that one translation unit and never reach a
// driver .c file); this fallback IS this device's real pin, not a
// placeholder, since this driver only ships on tdeck_plus/tdeck.
#ifndef BATTERY_ADC_CHANNEL
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_3
#endif

// Voltage divider ratio (RD2=100k, RD3=100k => 2.0), plus Meshtastic's own
// +10% correction factor for display-load undervoltage — same source.
#define BATTERY_ADC_MULTIPLIER 2.11f

#define BATTERY_POLL_MS (10 * 1000)

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t         s_cali = NULL;
static bool                      s_cali_ok = false;
static TaskHandle_t              s_task = NULL;

// Standard 1S LiPo open-circuit-voltage discharge curve — there's no fuel
// gauge IC here to do real coulomb counting, so this is an approximation
// (the same kind of piecewise table most no-fuel-gauge ESP32 battery
// projects use), not a precise reading. Good enough for a status-bar icon.
typedef struct { float mv; uint8_t pct; } curve_point_t;
static const curve_point_t s_curve[] = {
    { 4200, 100 }, { 4060, 90 }, { 3980, 80 }, { 3920, 70 },
    { 3870, 60 },  { 3820, 50 }, { 3790, 40 }, { 3770, 30 },
    { 3740, 20 },  { 3680, 10 }, { 3450, 0 },
};
#define CURVE_N (sizeof(s_curve) / sizeof(s_curve[0]))

static uint8_t voltage_to_percent(float mv)
{
    if (mv >= s_curve[0].mv) return 100;
    if (mv <= s_curve[CURVE_N - 1].mv) return 0;
    for (size_t i = 0; i < CURVE_N - 1; i++) {
        float hi = s_curve[i].mv, lo = s_curve[i + 1].mv;
        if (mv <= hi && mv >= lo) {
            float frac = (mv - lo) / (hi - lo);
            return (uint8_t)(s_curve[i + 1].pct + frac * (s_curve[i].pct - s_curve[i + 1].pct));
        }
    }
    return 0;
}

static void battery_poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, BATTERY_ADC_CHANNEL, &raw) == ESP_OK) {
            int pin_mv = raw;
            if (s_cali_ok) adc_cali_raw_to_voltage(s_cali, raw, &pin_mv);
            int batt_mv = (int)((float)pin_mv * BATTERY_ADC_MULTIPLIER);
            purr_kernel_set_battery_voltage_mv(batt_mv);
            purr_kernel_set_battery_percent(voltage_to_percent((float)batt_mv));
        } else {
            ESP_LOGW(TAG, "adc_oneshot_read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(BATTERY_POLL_MS));
    }
}

static int module_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = { .unit_id = ADC_UNIT_1 };
    if (adc_oneshot_new_unit(&unit_cfg, &s_adc) != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_new_unit failed");
        return -1;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten    = ADC_ATTEN_DB_12,   // full ~0-3.3V range — battery-divided input
    };
    if (adc_oneshot_config_channel(s_adc, BATTERY_ADC_CHANNEL, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "adc_oneshot_config_channel failed");
        return -1;
    }

    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = BATTERY_ADC_CHANNEL,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    // Calibration can fail on unfused/older-rev silicon — not fatal, just
    // falls back to raw-code voltage (less accurate, still usable for a
    // status icon rather than a precision measurement).
    s_cali_ok = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) == ESP_OK;
    if (!s_cali_ok) ESP_LOGW(TAG, "ADC calibration unavailable — using raw counts");

    xTaskCreate(battery_poll_task, "adc_battery", 3072, NULL, 2, &s_task);
    ESP_LOGI(TAG, "battery ADC ready (GPIO4/CH3, x%.2f, cali=%d)",
             (double)BATTERY_ADC_MULTIPLIER, s_cali_ok);
    return 0;
}

static void module_deinit(void)
{
    if (s_task) { vTaskDelete(s_task); s_task = NULL; }
    if (s_cali_ok) { adc_cali_delete_scheme_curve_fitting(s_cali); s_cali_ok = false; }
    if (s_adc) { adc_oneshot_del_unit(s_adc); s_adc = NULL; }
}

PURR_MODULE_REGISTER(adc_battery) = {
    .magic             = PURR_MODULE_MAGIC,
    .abi_version       = PURR_MODULE_ABI_VERSION,
    .module_type       = PURR_MOD_DRIVER,
    .load_priority     = PURR_PRIORITY_OPTIONAL,
    .name              = "adc_battery",
    .version           = "1.0.0",
    .kernel_min        = "0.11.1",
    .kernel_max        = "",
    .provided_catcalls = 0,
    .required_catcalls = 0,
    .init              = module_init,
    .deinit            = module_deinit,
};
