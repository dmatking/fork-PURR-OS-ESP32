#include "lora_manager.h"
#include <Arduino.h>

// RAK3172 (STM32WL) — LoRa P2P mode via UART AT commands.
// Unsolicited RX event format: +EVT:RXP2P:<rssi>:<snr>:<hex_payload>

static bool     lora_ready      = false;
static bool     lora_yield_flag = false;
static bool     tx_busy         = false;

static uint32_t cfg_freq  = 915000000;
static uint8_t  cfg_power = 14;
static uint8_t  cfg_sf    = 7;
static uint8_t  cfg_bw    = 0;   // 0=125kHz 1=250kHz 2=500kHz
static uint8_t  cfg_cr    = 0;   // 0=4/5 1=4/6 2=4/7 3=4/8

static int      last_rssi = 0;
static float    last_snr  = 0.0f;

static constexpr size_t RX_BUF_SIZE = 256;
static uint8_t  rx_data[RX_BUF_SIZE];
static size_t   rx_len       = 0;
static bool     rx_available = false;

static char     line_buf[320];
static size_t   line_pos = 0;

// ── AT helpers ────────────────────────────────────────────────────────────────

static bool at_send(const char* cmd, uint32_t timeout_ms = 500) {
    while (Serial2.available()) Serial2.read();
    Serial2.println(cmd);
    Serial.printf("[lora] >> %s\n", cmd);

    char resp[128] = {};
    size_t pos = 0;
    uint32_t start = millis();
    while (millis() - start < timeout_ms) {
        while (Serial2.available() && pos < sizeof(resp) - 1)
            resp[pos++] = Serial2.read();
        resp[pos] = '\0';
        if (strstr(resp, "OK") || strstr(resp, "ERROR")) break;
        delay(1);
    }
    bool ok = strstr(resp, "OK") != nullptr;
    Serial.printf("[lora] << %s (%s)\n", resp, ok ? "OK" : "ERR");
    return ok;
}

static void apply_p2p_config() {
    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+PFREQ=%lu", (unsigned long)cfg_freq);
    at_send(cmd);
    snprintf(cmd, sizeof(cmd), "AT+PSF=%u", cfg_sf);    at_send(cmd);
    snprintf(cmd, sizeof(cmd), "AT+PBW=%u", cfg_bw);    at_send(cmd);
    snprintf(cmd, sizeof(cmd), "AT+PCR=%u", cfg_cr);    at_send(cmd);
    snprintf(cmd, sizeof(cmd), "AT+PTP=%u", cfg_power); at_send(cmd);
    at_send("AT+PRECV=65535");  // continuous RX
}

static void parse_line(const char* line) {
    if (strstr(line, "TXP2P DONE") || strstr(line, "SEND_CONFIRMED")) {
        tx_busy = false;
        return;
    }
    if (strncmp(line, "+EVT:RXP2P:", 11) == 0) {
        const char* p = line + 11;
        last_rssi = atoi(p);
        p = strchr(p, ':'); if (!p) return;
        last_snr  = atof(++p);
        p = strchr(p, ':'); if (!p) return;
        ++p;
        rx_len = 0;
        while (*p && *(p+1) && rx_len < RX_BUF_SIZE) {
            char b[3] = { p[0], p[1], '\0' };
            rx_data[rx_len++] = (uint8_t)strtol(b, nullptr, 16);
            p += 2;
        }
        rx_available = (rx_len > 0);
        at_send("AT+PRECV=65535");
    }
}

// ── Public API ────────────────────────────────────────────────────────────────

void lora_manager_init(uint32_t freq_hz, uint8_t power_dbm) {
    cfg_freq  = freq_hz;
    cfg_power = power_dbm;
    Serial2.begin(LORA_UART_BAUD, SERIAL_8N1, LORA_UART_RX, LORA_UART_TX);
    delay(500);
    if (!at_send("AT", 1000))         { Serial.println("[lora] RAK3172 no response"); return; }
    if (!at_send("AT+NWM=0", 2000))   { Serial.println("[lora] P2P mode failed");    return; }
    apply_p2p_config();
    lora_ready = true;
    Serial.printf("[lora] RAK3172 OK %lu Hz %ddBm SF%u\n",
                  (unsigned long)freq_hz, power_dbm, cfg_sf);
}

void lora_manager_update() {
    while (Serial2.available()) {
        char c = Serial2.read();
        if (c == '\n') {
            line_buf[line_pos] = '\0';
            if (line_pos > 0) parse_line(line_buf);
            line_pos = 0;
        } else if (c != '\r' && line_pos < sizeof(line_buf) - 1) {
            line_buf[line_pos++] = c;
        }
    }
}

void lora_manager_deinit() {
    if (lora_ready) { at_send("AT+SLEEP=0"); lora_ready = false; }
}

bool     lora_manager_enabled()        { return lora_ready && !lora_yield_flag; }
uint32_t lora_manager_get_frequency()  { return cfg_freq; }
uint8_t  lora_manager_get_power()      { return cfg_power; }
int      lora_manager_rssi()           { return last_rssi; }
float    lora_manager_snr()            { return last_snr; }
bool     lora_manager_busy()           { return tx_busy; }
bool     lora_manager_data_available() { return rx_available; }

void lora_manager_set_frequency(uint32_t f) {
    cfg_freq = f;
    if (!lora_ready) return;
    char cmd[32]; snprintf(cmd, sizeof(cmd), "AT+PFREQ=%lu", (unsigned long)f);
    at_send(cmd);
}
void lora_manager_set_power(uint8_t dbm) {
    cfg_power = dbm;
    if (!lora_ready) return;
    char cmd[16]; snprintf(cmd, sizeof(cmd), "AT+PTP=%u", dbm);
    at_send(cmd);
}
void lora_manager_set_spreading_factor(uint8_t sf) {
    cfg_sf = sf;
    if (!lora_ready) return;
    char cmd[16]; snprintf(cmd, sizeof(cmd), "AT+PSF=%u", sf);
    at_send(cmd);
}
void lora_manager_set_bandwidth(uint32_t bw_hz) {
    cfg_bw = (bw_hz >= 500000) ? 2 : (bw_hz >= 250000) ? 1 : 0;
    if (!lora_ready) return;
    char cmd[16]; snprintf(cmd, sizeof(cmd), "AT+PBW=%u", cfg_bw);
    at_send(cmd);
}
void lora_manager_set_coding_rate(uint8_t cr) {
    cfg_cr = (cr >= 5) ? (cr - 5) : cr;
    if (!lora_ready) return;
    char cmd[16]; snprintf(cmd, sizeof(cmd), "AT+PCR=%u", cfg_cr);
    at_send(cmd);
}
void lora_manager_set_sync_word(uint8_t sw) { (void)sw; }  // not exposed via RAK3172 AT

bool lora_manager_send(const uint8_t* data, size_t len) {
    if (!lora_ready || lora_yield_flag || tx_busy) return false;
    at_send("AT+PRECV=0");
    char cmd[4 + RX_BUF_SIZE * 2 + 1] = "AT+PSEND=";
    size_t pos = strlen(cmd);
    for (size_t i = 0; i < len && pos < sizeof(cmd) - 2; i++) {
        snprintf(cmd + pos, 3, "%02X", data[i]); pos += 2;
    }
    tx_busy = true;
    bool ok = at_send(cmd, 3000);
    if (!ok) tx_busy = false;
    at_send("AT+PRECV=65535");
    return ok;
}

size_t lora_manager_read(uint8_t* buf, size_t max_len) {
    if (!rx_available) return 0;
    size_t n = (rx_len < max_len) ? rx_len : max_len;
    memcpy(buf, rx_data, n);
    rx_available = false; rx_len = 0;
    return n;
}

void lora_manager_yield() {
    lora_yield_flag = true;
    if (lora_ready) { at_send("AT+PRECV=0"); at_send("AT+SLEEP=0"); }
}
void lora_manager_reclaim() {
    lora_yield_flag = false;
    if (lora_ready) apply_p2p_config();
}
bool lora_manager_yielded() { return lora_yield_flag; }
