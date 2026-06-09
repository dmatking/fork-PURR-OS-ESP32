// cc1101_manager.cpp — CC1101 sub-GHz FSK radio via RadioLib
// Target: LilyGo T-Embed CC1101 (ESP32-S3, SPI2)

#include "cc1101_manager.h"
#include <RadioLib.h>
#include "driver/spi_master.h"
#include "esp_log.h"
#include <string.h>

static const char* TAG = "cc1101";

static CC1101* s_radio      = nullptr;
static SPIClass* s_spi      = nullptr;
static bool s_enabled       = false;
static bool s_yielded       = false;
static float s_freq_mhz     = 433.92f;
static int8_t s_power_dbm   = 10;

static volatile bool s_rx_flag = false;
static uint8_t s_rx_buf[64];
static size_t  s_rx_len = 0;

static void IRAM_ATTR rx_isr() {
    s_rx_flag = true;
}

void cc1101_manager_init(float freq_mhz, float bitrate_kbps, float freq_dev_khz,
                          float rx_bw_khz, int8_t power_dbm) {
    s_freq_mhz   = freq_mhz;
    s_power_dbm  = power_dbm;

    s_spi = new SPIClass(HSPI);
    s_spi->begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);

    SPISettings spi_cfg(4000000, MSBFIRST, SPI_MODE0);
    s_radio = new CC1101(new Module(CC1101_CS, CC1101_GDO0, RADIOLIB_NC, CC1101_GDO2,
                                    *s_spi, spi_cfg));

    int state = s_radio->begin(freq_mhz, bitrate_kbps, freq_dev_khz, rx_bw_khz,
                                power_dbm, 16);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "begin() failed: %d", state);
        delete s_radio; s_radio = nullptr;
        return;
    }

    s_radio->setGdo0Action(rx_isr);
    s_radio->startReceive();
    s_enabled = true;
    ESP_LOGI(TAG, "OK %.2f MHz %.1f kbps %d dBm", freq_mhz, bitrate_kbps, power_dbm);
}

void cc1101_manager_deinit() {
    if (!s_radio) return;
    s_radio->sleep();
    delete s_radio; s_radio = nullptr;
    s_enabled = false;
}

void cc1101_manager_update() {
    if (!s_enabled || s_yielded || !s_rx_flag) return;
    s_rx_flag = false;

    int state = s_radio->readData(s_rx_buf, sizeof(s_rx_buf));
    if (state == RADIOLIB_ERR_NONE) {
        s_rx_len = s_radio->getPacketLength();
        ESP_LOGD(TAG, "rx %u bytes RSSI=%d", (unsigned)s_rx_len, (int)s_radio->getRSSI());
    }
    s_radio->startReceive();
}

bool cc1101_manager_enabled()  { return s_enabled; }
bool cc1101_manager_yielded()  { return s_yielded; }

void cc1101_manager_set_frequency(float freq_mhz) {
    s_freq_mhz = freq_mhz;
    if (s_radio) s_radio->setFrequency(freq_mhz);
}

void cc1101_manager_set_power(int8_t dbm) {
    s_power_dbm = dbm;
    if (s_radio) s_radio->setOutputPower(dbm);
}

float   cc1101_manager_get_frequency() { return s_freq_mhz; }
int8_t  cc1101_manager_get_power()     { return s_power_dbm; }
int     cc1101_manager_rssi()          { return s_radio ? (int)s_radio->getRSSI() : -127; }

bool cc1101_manager_send(const uint8_t* data, size_t len) {
    if (!s_enabled || s_yielded || !s_radio) return false;
    int state = s_radio->transmit(data, len);
    s_radio->startReceive();
    return (state == RADIOLIB_ERR_NONE);
}

bool cc1101_manager_busy() {
    return s_enabled && s_radio && !s_radio->isChannelFree(s_freq_mhz, -85);
}

bool cc1101_manager_data_available() {
    return s_rx_len > 0;
}

size_t cc1101_manager_read(uint8_t* buf, size_t max_len) {
    if (!s_rx_len) return 0;
    size_t n = (s_rx_len < max_len) ? s_rx_len : max_len;
    memcpy(buf, s_rx_buf, n);
    s_rx_len = 0;
    return n;
}

void cc1101_manager_yield() {
    if (!s_enabled || !s_radio) return;
    s_radio->sleep();
    s_yielded = true;
}

void cc1101_manager_reclaim() {
    if (!s_yielded || !s_radio) return;
    s_radio->startReceive();
    s_yielded = false;
}
