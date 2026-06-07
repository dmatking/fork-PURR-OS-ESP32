// gps_manager.cpp — GPS NMEA parser via IDF UART driver (pure ESP-IDF)

#include "gps_manager.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char* TAG = "gps";

#define GPS_UART_NUM  UART_NUM_1
#define GPS_RX_BUF   512

static bool    s_enabled    = false;
static bool    s_has_fix    = false;
static double  s_lat        = 0.0;
static double  s_lon        = 0.0;
static float   s_alt        = 0.0f;
static float   s_speed      = 0.0f;
static float   s_heading    = 0.0f;
static int     s_sats_used  = 0;
static int     s_sats_vis   = 0;
static float   s_hdop       = 99.9f;
static uint8_t  s_hour = 0, s_min = 0, s_sec = 0;
static uint16_t s_year = 0;
static uint8_t  s_month = 0, s_day = 0;
static char     s_last_nmea[128] = {};

static bool uart_init_at(uint32_t baud) {
    uart_config_t cfg = {
        .baud_rate  = (int)baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    if (uart_param_config(GPS_UART_NUM, &cfg) != ESP_OK) return false;
    uart_set_pin(GPS_UART_NUM, GPS_UART_TX_PIN, GPS_UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (uart_driver_install(GPS_UART_NUM, GPS_RX_BUF, 0, 0, NULL, 0) != ESP_OK)
        return false;
    return true;
}

static bool probe_baud(uint32_t baud, uint32_t timeout_ms) {
    if (!uart_init_at(baud)) return false;
    uint32_t deadline = (uint32_t)(esp_timer_get_time() / 1000) + timeout_ms;
    uint8_t ch;
    while ((uint32_t)(esp_timer_get_time() / 1000) < deadline) {
        int n = uart_read_bytes(GPS_UART_NUM, &ch, 1, pdMS_TO_TICKS(10));
        if (n > 0 && ch == '$') return true;
    }
    uart_driver_delete(GPS_UART_NUM);
    return false;
}

static void parse_rmc(const char* s) {
    char buf[128];
    strncpy(buf, s, sizeof(buf) - 1);
    char* fields[15] = {};
    int n = 0;
    for (char* p = strtok(buf, ","); p && n < 15; p = strtok(nullptr, ","))
        fields[n++] = p;
    if (n < 10) return;

    s_has_fix = (fields[2] && fields[2][0] == 'A');
    if (!s_has_fix) return;

    if (fields[1] && strlen(fields[1]) >= 6) {
        char tmp[3] = {};
        tmp[0] = fields[1][0]; tmp[1] = fields[1][1]; s_hour  = (uint8_t)atoi(tmp);
        tmp[0] = fields[1][2]; tmp[1] = fields[1][3]; s_min   = (uint8_t)atoi(tmp);
        tmp[0] = fields[1][4]; tmp[1] = fields[1][5]; s_sec   = (uint8_t)atoi(tmp);
    }
    if (fields[3] && fields[4]) {
        double raw = atof(fields[3]);
        int deg = (int)(raw / 100);
        s_lat = deg + (raw - deg * 100) / 60.0;
        if (fields[4][0] == 'S') s_lat = -s_lat;
    }
    if (fields[5] && fields[6]) {
        double raw = atof(fields[5]);
        int deg = (int)(raw / 100);
        s_lon = deg + (raw - deg * 100) / 60.0;
        if (fields[6][0] == 'W') s_lon = -s_lon;
    }
    if (fields[7]) s_speed   = (float)(atof(fields[7]) * 1.852);
    if (fields[8]) s_heading = (float)atof(fields[8]);
    if (fields[9] && strlen(fields[9]) >= 6) {
        char tmp[3] = {};
        tmp[0] = fields[9][0]; tmp[1] = fields[9][1]; s_day   = (uint8_t)atoi(tmp);
        tmp[0] = fields[9][2]; tmp[1] = fields[9][3]; s_month = (uint8_t)atoi(tmp);
        tmp[0] = fields[9][4]; tmp[1] = fields[9][5]; s_year  = (uint16_t)(2000 + atoi(tmp));
    }
}

void gps_manager_init() {
    vTaskDelay(pdMS_TO_TICKS(GPS_INIT_WAIT_MS));

    if (probe_baud(GPS_UART_BAUD_DEFAULT, 1500)) { s_enabled = true; return; }
    if (probe_baud(GPS_UART_BAUD_FALLBACK, 1500)) { s_enabled = true; return; }
    ESP_LOGW(TAG, "GPS module not detected");
}

void gps_manager_update() {
    if (!s_enabled) return;

    static char line[128];
    static int  line_len = 0;
    uint8_t ch;

    size_t avail = 0;
    uart_get_buffered_data_len(GPS_UART_NUM, &avail);

    for (size_t i = 0; i < avail; i++) {
        if (uart_read_bytes(GPS_UART_NUM, &ch, 1, 0) <= 0) break;
        if (ch == '$') line_len = 0;
        if (line_len < (int)sizeof(line) - 1) line[line_len++] = (char)ch;
        if (ch == '\n' && line_len > 6) {
            line[line_len] = '\0';
            strncpy(s_last_nmea, line, sizeof(s_last_nmea) - 1);
            if (strncmp(line, "$GNRMC", 6) == 0 || strncmp(line, "$GPRMC", 6) == 0)
                parse_rmc(line);
            line_len = 0;
        }
    }
}

void gps_manager_deinit() {
    if (s_enabled) uart_driver_delete(GPS_UART_NUM);
    s_enabled = s_has_fix = false;
}

bool   gps_manager_enabled()            { return s_enabled; }
bool   gps_manager_has_fix()            { return s_has_fix; }
double gps_manager_latitude()           { return s_lat; }
double gps_manager_longitude()          { return s_lon; }
float  gps_manager_altitude_m()         { return s_alt; }
float  gps_manager_speed_kmh()          { return s_speed; }
float  gps_manager_heading_deg()        { return s_heading; }
int    gps_manager_satellites_used()    { return s_sats_used; }
int    gps_manager_satellites_visible() { return s_sats_vis; }
float  gps_manager_hdop()               { return s_hdop; }

void gps_manager_time(uint8_t* h, uint8_t* m, uint8_t* s)   { *h=s_hour; *m=s_min; *s=s_sec; }
void gps_manager_date(uint16_t* y, uint8_t* mo, uint8_t* d)  { *y=s_year; *mo=s_month; *d=s_day; }
const char* gps_manager_last_nmea()                           { return s_last_nmea; }
