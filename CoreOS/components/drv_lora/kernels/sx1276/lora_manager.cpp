#include "lora_manager.h"
#include "esp_timer.h"
#include <SPI.h>
#include <LoRa.h>

// SX1276 / RFM95W via SPI + arduino-LoRa library.
// Key difference from SX1262: interrupt pin is DIO0, not DIO1.
// SX1276 is limited to 17 dBm without PA_BOOST; RFM95W supports up to 20 dBm with PA_BOOST.

static bool     lora_ready      = false;
static bool     lora_yield_flag = false;
static uint32_t lora_freq       = 915000000;
static uint8_t  lora_power      = 17;

void lora_manager_init(uint32_t freq_hz, uint8_t power_dbm) {
    lora_freq  = freq_hz;
    lora_power = power_dbm;

    LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

    if (!LoRa.begin(freq_hz)) {
        Serial.println("[lora] SX1276 init failed");
        return;
    }

    LoRa.setTxPower(power_dbm);
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.setSyncWord(0x12);
    LoRa.enableCrc();

    lora_ready = true;
    Serial.printf("[lora] SX1276 OK %lu Hz %ddBm\n", (unsigned long)freq_hz, power_dbm);
}

void lora_manager_update() {}  // polled on-demand via LoRa.parsePacket()

void lora_manager_deinit() {
    if (lora_ready) { LoRa.end(); lora_ready = false; }
}

bool     lora_manager_enabled()        { return lora_ready && !lora_yield_flag; }
uint32_t lora_manager_get_frequency()  { return lora_freq; }
uint8_t  lora_manager_get_power()      { return lora_power; }
int      lora_manager_rssi()           { return lora_ready ? LoRa.packetRssi() : 0; }
float    lora_manager_snr()            { return lora_ready ? LoRa.packetSnr()  : 0.0f; }
bool     lora_manager_busy()           { return false; }
bool     lora_manager_data_available() { return !lora_yield_flag && lora_ready && LoRa.parsePacket() > 0; }

void lora_manager_set_frequency(uint32_t f) {
    lora_freq = f;
    if (lora_ready) LoRa.setFrequency(f);
}
void lora_manager_set_power(uint8_t dbm) {
    lora_power = dbm;
    if (lora_ready) LoRa.setTxPower(dbm);
}
void lora_manager_set_spreading_factor(uint8_t sf) {
    if (lora_ready) LoRa.setSpreadingFactor(sf);
}
void lora_manager_set_bandwidth(uint32_t bw_hz) {
    if (lora_ready) LoRa.setSignalBandwidth(bw_hz);
}
void lora_manager_set_coding_rate(uint8_t cr) {
    if (lora_ready) LoRa.setCodingRate4(cr);
}
void lora_manager_set_sync_word(uint8_t sw) {
    if (lora_ready) LoRa.setSyncWord(sw);
}

bool lora_manager_send(const uint8_t* data, size_t len) {
    if (!lora_ready || lora_yield_flag) return false;
    LoRa.beginPacket();
    LoRa.write(data, len);
    return LoRa.endPacket();
}

size_t lora_manager_read(uint8_t* buf, size_t max_len) {
    size_t n = 0;
    while (LoRa.available() && n < max_len)
        buf[n++] = LoRa.read();
    return n;
}

void lora_manager_yield() {
    lora_yield_flag = true;
    if (lora_ready) LoRa.sleep();
}
void lora_manager_reclaim() {
    lora_yield_flag = false;
    if (lora_ready) LoRa.idle();
}
bool lora_manager_yielded() { return lora_yield_flag; }
