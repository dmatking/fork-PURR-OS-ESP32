#include "pi_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "pi";

// Forward declarations to kitt.cpp — pi_manager calls back into KITT
// to trigger display yield/reclaim without a circular include.
extern void kitt_display_yield_to_pi();
extern void kitt_display_reclaim_from_pi();

static pi_state_t pi_state      = PI_STATE_ABSENT;
static pi_state_t last_pi_state = PI_STATE_ABSENT;
static bool       gate_on       = false;

void pi_manager_init() {
    gpio_set_direction((gpio_num_t)PI_GATE_PIN,      GPIO_MODE_OUTPUT);
    gpio_set_direction((gpio_num_t)PI_HANDSHAKE_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode((gpio_num_t)PI_HANDSHAKE_PIN, GPIO_PULLDOWN_ONLY);
    gpio_set_level((gpio_num_t)PI_GATE_PIN, 0);
    gate_on = false;
    pi_manager_update();
    ESP_LOGI(TAG, "init OK");
}

void pi_manager_update() {
    bool gate      = (bool)gpio_get_level((gpio_num_t)PI_GATE_PIN);
    bool handshake = (bool)gpio_get_level((gpio_num_t)PI_HANDSHAKE_PIN);

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
    gpio_set_level((gpio_num_t)PI_GATE_PIN, 1);
    gate_on = true;
    ESP_LOGI(TAG, "rail enabled, waiting for handshake...");

    uint32_t start = (uint32_t)(esp_timer_get_time() / 1000ULL);
    while (!gpio_get_level((gpio_num_t)PI_HANDSHAKE_PIN) &&
           (uint32_t)(esp_timer_get_time() / 1000ULL) - start < 30000)
        vTaskDelay(pdMS_TO_TICKS(100));

    if (gpio_get_level((gpio_num_t)PI_HANDSHAKE_PIN))
        ESP_LOGI(TAG, "CM5 active");
    else
        ESP_LOGW(TAG, "CM5 handshake timeout");
}

void pi_manager_power_off() {
    // Send HALT to CM5 over UART1
    const uart_config_t uart_cfg = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_1, &uart_cfg);
    uart_set_pin(UART_NUM_1, PI_UART_TX, PI_UART_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_NUM_1, 256, 0, 0, NULL, 0);
    uart_write_bytes(UART_NUM_1, "HALT\r\n", 6);
    uart_driver_delete(UART_NUM_1);

    ESP_LOGI(TAG, "halt sent, waiting for CM5 shutdown...");

    uint32_t start = (uint32_t)(esp_timer_get_time() / 1000ULL);
    while (gpio_get_level((gpio_num_t)PI_HANDSHAKE_PIN) &&
           (uint32_t)(esp_timer_get_time() / 1000ULL) - start < 10000)
        vTaskDelay(pdMS_TO_TICKS(100));

    gpio_set_level((gpio_num_t)PI_GATE_PIN, 0);
    gate_on = false;
    ESP_LOGI(TAG, "rail disabled");
}

pi_state_t pi_manager_state()          { return pi_state; }
bool       pi_manager_handshake_high() { return (bool)gpio_get_level((gpio_num_t)PI_HANDSHAKE_PIN); }
bool       pi_manager_rail_enabled()   { return gate_on; }
