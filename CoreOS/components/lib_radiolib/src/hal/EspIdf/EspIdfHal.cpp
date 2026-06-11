#include "EspIdfHal.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include <string.h>

// spi_device_handle_t is spi_device_t*, stored as void* in header to avoid
// exposing IDF SPI headers to callers.
#define DEV() ((spi_device_handle_t)_dev)

EspIdfHal::EspIdfHal(int host, int sck, int miso, int mosi, int cs, uint32_t freq_hz)
    : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1,
                  GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE),
      _host(host), _sck(sck), _miso(miso), _mosi(mosi), _cs(cs), _freq_hz(freq_hz) {}

void EspIdfHal::init() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num   = _mosi;
    bus.miso_io_num   = _miso;
    bus.sclk_io_num   = _sck;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 256;
    spi_bus_initialize((spi_host_device_t)_host, &bus, SPI_DMA_DISABLED);

    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = _freq_hz;
    dev.mode           = 0;
    dev.spics_io_num   = _cs;
    dev.queue_size     = 1;
    spi_device_handle_t h = nullptr;
    spi_bus_add_device((spi_host_device_t)_host, &dev, &h);
    _dev = h;
}

void EspIdfHal::term() {
    if (_dev) { spi_bus_remove_device(DEV()); _dev = nullptr; }
    spi_bus_free((spi_host_device_t)_host);
}

void EspIdfHal::pinMode(uint32_t pin, uint32_t mode) {
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode         = (gpio_mode_t)mode;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void EspIdfHal::digitalWrite(uint32_t pin, uint32_t value) {
    gpio_set_level((gpio_num_t)pin, value ? 1 : 0);
}

uint32_t EspIdfHal::digitalRead(uint32_t pin) {
    return (uint32_t)gpio_get_level((gpio_num_t)pin);
}

void EspIdfHal::attachInterrupt(uint32_t pin, void (*cb)(void), uint32_t mode) {
    gpio_set_intr_type((gpio_num_t)pin, (gpio_int_type_t)mode);
    gpio_install_isr_service(0);
    gpio_isr_handler_add((gpio_num_t)pin, (gpio_isr_t)(void*)cb, nullptr);
    gpio_intr_enable((gpio_num_t)pin);
}

void EspIdfHal::detachInterrupt(uint32_t pin) {
    gpio_isr_handler_remove((gpio_num_t)pin);
    gpio_intr_disable((gpio_num_t)pin);
}

void EspIdfHal::delay(RadioLibTime_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1));
}

void EspIdfHal::delayMicroseconds(RadioLibTime_t us) {
    if (us < 20) {
        uint64_t end = esp_timer_get_time() + us;
        while (esp_timer_get_time() < end) {}
    } else {
        vTaskDelay(pdMS_TO_TICKS((us + 999) / 1000));
    }
}

RadioLibTime_t EspIdfHal::millis() {
    return (RadioLibTime_t)(esp_timer_get_time() / 1000ULL);
}

RadioLibTime_t EspIdfHal::micros() {
    return (RadioLibTime_t)esp_timer_get_time();
}

long EspIdfHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) {
    uint64_t start = esp_timer_get_time();
    while ((uint32_t)gpio_get_level((gpio_num_t)pin) != state) {
        if (esp_timer_get_time() - start > timeout * 1000ULL) return 0;
    }
    uint64_t t0 = esp_timer_get_time();
    while ((uint32_t)gpio_get_level((gpio_num_t)pin) == state) {
        if (esp_timer_get_time() - start > timeout * 1000ULL) return 0;
    }
    return (long)((esp_timer_get_time() - t0) / 1000ULL);
}

void EspIdfHal::spiBegin()            { /* init() already called */ }
void EspIdfHal::spiBeginTransaction() { if (_dev) spi_device_acquire_bus(DEV(), portMAX_DELAY); }
void EspIdfHal::spiEndTransaction()   { if (_dev) spi_device_release_bus(DEV()); }
void EspIdfHal::spiEnd()             { /* term() handles teardown */ }

void EspIdfHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in) {
    if (!_dev || !len) return;
    spi_transaction_t t = {};
    t.length    = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;
    spi_device_transmit(DEV(), &t);
}

void EspIdfHal::yield() {
    taskYIELD();
}
