// lora_manager.cpp — LoRa SX1262 manager via RadioLib (pure IDF-compatible)
// RadioLib component at CoreOS/components/radiolib/ — no Arduino dependency.

#include "lora_manager.h"
#include "RadioLib.h"
#include "esp_log.h"

static const char* TAG = "lora";

// RadioLib IDF HAL — uses ESP-IDF SPI master directly
// SX1262 on Heltec V3: CS=8, DIO1=14, RST=12, BUSY=13, SCK=9, MOSI=10, MISO=11
static SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RST, LORA_BUSY);

static bool     lora_ready      = false;
static bool     lora_yield_flag = false;
static uint32_t lora_freq       = 915000000;
static uint8_t  lora_power      = 14;

// Receive buffer
#define RX_BUF_SIZE 256
static uint8_t  rx_buf[RX_BUF_SIZE];
static size_t   rx_len = 0;
static bool     rx_ready = false;

void lora_manager_init(uint32_t freq_hz, uint8_t power_dbm) {
    lora_freq  = freq_hz;
    lora_power = power_dbm;

    float freq_mhz = (float)freq_hz / 1e6f;
    int state = radio.begin(freq_mhz);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "SX1262 init failed: %d", state);
        return;
    }

    radio.setOutputPower(power_dbm);
    radio.setSpreadingFactor(7);
    radio.setBandwidth(125.0f);
    radio.setCodingRate(5);
    radio.setSyncWord(0x12);
    radio.setCRC(true);

    lora_ready = true;
    ESP_LOGI(TAG, "SX1262 OK %.1f MHz %ddBm", freq_mhz, power_dbm);
}

void lora_manager_update() {
    if (!lora_ready || lora_yield_flag || rx_ready) return;
    int state = radio.receive(rx_buf, sizeof(rx_buf));
    if (state == RADIOLIB_ERR_NONE) {
        rx_len   = radio.getPacketLength();
        rx_ready = true;
    }
}

void lora_manager_deinit() {
    if (lora_ready) { radio.sleep(); lora_ready = false; }
}

bool     lora_manager_enabled()        { return lora_ready && !lora_yield_flag; }
uint32_t lora_manager_get_frequency()  { return lora_freq; }
uint8_t  lora_manager_get_power()      { return lora_power; }
int      lora_manager_rssi()           { return lora_ready ? (int)radio.getRSSI() : 0; }
float    lora_manager_snr()            { return lora_ready ? radio.getSNR() : 0.0f; }
bool     lora_manager_busy()           { return false; }
bool     lora_manager_data_available() { return rx_ready; }

void lora_manager_set_frequency(uint32_t f) {
    lora_freq = f;
    if (lora_ready) radio.setFrequency((float)f / 1e6f);
}
void lora_manager_set_power(uint8_t dbm) {
    lora_power = dbm;
    if (lora_ready) radio.setOutputPower(dbm);
}
void lora_manager_set_spreading_factor(uint8_t sf) {
    if (lora_ready) radio.setSpreadingFactor(sf);
}
void lora_manager_set_bandwidth(uint32_t bw_hz) {
    if (lora_ready) radio.setBandwidth((float)bw_hz / 1000.0f);
}
void lora_manager_set_coding_rate(uint8_t cr) {
    if (lora_ready) radio.setCodingRate(cr);
}
void lora_manager_set_sync_word(uint8_t sw) {
    if (lora_ready) radio.setSyncWord(sw);
}

bool lora_manager_send(const uint8_t* data, size_t len) {
    if (!lora_ready || lora_yield_flag) return false;
    return radio.transmit((uint8_t*)data, len) == RADIOLIB_ERR_NONE;
}

size_t lora_manager_read(uint8_t* buf, size_t max_len) {
    if (!rx_ready) return 0;
    size_t n = (rx_len < max_len) ? rx_len : max_len;
    memcpy(buf, rx_buf, n);
    rx_ready = false;
    rx_len   = 0;
    return n;
}

void lora_manager_yield() {
    lora_yield_flag = true;
    if (lora_ready) radio.sleep();
}
void lora_manager_reclaim() {
    lora_yield_flag = false;
    if (lora_ready) radio.standby();
}
bool lora_manager_yielded() { return lora_yield_flag; }
