#include "gps_manager.h"
#include <Arduino.h>
#include <HardwareSerial.h>

// TODO: replace raw NMEA parsing with TinyGPS++ once dependency is added
// idf_component.yml: add mikalhart/tinygpsplus or bundle manually

static HardwareSerial gpsSerial(1);  // UART1 — Grove pins repurposed on T-Deck Plus

static bool s_enabled   = false;
static bool s_has_fix   = false;
static double s_lat     = 0.0;
static double s_lon     = 0.0;
static float  s_alt     = 0.0f;
static float  s_speed   = 0.0f;
static float  s_heading = 0.0f;
static int    s_sats_used    = 0;
static int    s_sats_visible = 0;
static float  s_hdop    = 99.9f;
static uint8_t  s_hour = 0, s_min = 0, s_sec = 0;
static uint16_t s_year = 0;
static uint8_t  s_month = 0, s_day = 0;
static char s_last_nmea[128] = {};

// Probe the module at a given baud. Waits up to timeout_ms for any '$' byte
// (start of NMEA sentence). Returns true if the module responds.
static bool probe_baud(uint32_t baud, uint32_t timeout_ms) {
    gpsSerial.begin(baud, SERIAL_8N1, GPS_UART_RX_PIN, GPS_UART_TX_PIN);
    uint32_t deadline = millis() + timeout_ms;
    while (millis() < deadline) {
        if (gpsSerial.available() && gpsSerial.peek() == '$') return true;
        delay(10);
    }
    gpsSerial.end();
    return false;
}

void gps_manager_init() {
    // Wait for module power-on before probing. Skipping this causes false
    // "no GPS" results (see MIA-M10Q header note).
    delay(GPS_INIT_WAIT_MS);

    if (probe_baud(GPS_UART_BAUD_DEFAULT, 1500)) {
        s_enabled = true;
        return;
    }
    if (probe_baud(GPS_UART_BAUD_FALLBACK, 1500)) {
        s_enabled = true;
        return;
    }
    // Module not detected — leave serial closed, s_enabled = false.
}

// Minimal $GNRMC / $GPRMC parser — replace with TinyGPS++ for robustness
static void parse_rmc(const char* s) {
    // $GNRMC,HHMMSS.ss,A,LLLL.LL,N,YYYYY.YY,E,spd,hdg,DDMMYY,...
    // Field indices: 0=tag 1=time 2=status 3=lat 4=N/S 5=lon 6=E/W 7=speed 8=course 9=date
    char buf[128];
    strncpy(buf, s, sizeof(buf) - 1);
    char* fields[15] = {};
    int n = 0;
    for (char* p = strtok(buf, ","); p && n < 15; p = strtok(nullptr, ","))
        fields[n++] = p;
    if (n < 10) return;

    s_has_fix = (fields[2] && fields[2][0] == 'A');
    if (!s_has_fix) return;

    // Time: HHMMSS.ss
    if (fields[1] && strlen(fields[1]) >= 6) {
        char tmp[3] = {};
        tmp[0] = fields[1][0]; tmp[1] = fields[1][1]; s_hour  = (uint8_t)atoi(tmp);
        tmp[0] = fields[1][2]; tmp[1] = fields[1][3]; s_min   = (uint8_t)atoi(tmp);
        tmp[0] = fields[1][4]; tmp[1] = fields[1][5]; s_sec   = (uint8_t)atoi(tmp);
    }
    // Latitude: DDMM.MMMM
    if (fields[3] && fields[4]) {
        double raw = atof(fields[3]);
        int deg = (int)(raw / 100);
        double min = raw - deg * 100;
        s_lat = deg + min / 60.0;
        if (fields[4][0] == 'S') s_lat = -s_lat;
    }
    // Longitude: DDDMM.MMMM
    if (fields[5] && fields[6]) {
        double raw = atof(fields[5]);
        int deg = (int)(raw / 100);
        double min = raw - deg * 100;
        s_lon = deg + min / 60.0;
        if (fields[6][0] == 'W') s_lon = -s_lon;
    }
    // Speed: knots → km/h
    if (fields[7]) s_speed   = (float)(atof(fields[7]) * 1.852);
    // Course
    if (fields[8]) s_heading = (float)atof(fields[8]);
    // Date: DDMMYY
    if (fields[9] && strlen(fields[9]) >= 6) {
        char tmp[3] = {};
        tmp[0] = fields[9][0]; tmp[1] = fields[9][1]; s_day   = (uint8_t)atoi(tmp);
        tmp[0] = fields[9][2]; tmp[1] = fields[9][3]; s_month = (uint8_t)atoi(tmp);
        tmp[0] = fields[9][4]; tmp[1] = fields[9][5]; s_year  = (uint16_t)(2000 + atoi(tmp));
    }
}

void gps_manager_update() {
    if (!s_enabled) return;

    // Read bytes into line buffer; parse complete sentences
    static char line[128];
    static int  line_len = 0;

    while (gpsSerial.available()) {
        char c = (char)gpsSerial.read();
        if (c == '$') line_len = 0;  // start of new sentence
        if (line_len < (int)sizeof(line) - 1) line[line_len++] = c;
        if (c == '\n' && line_len > 6) {
            line[line_len] = '\0';
            strncpy(s_last_nmea, line, sizeof(s_last_nmea) - 1);
            if (strncmp(line, "$GNRMC", 6) == 0 || strncmp(line, "$GPRMC", 6) == 0)
                parse_rmc(line);
            line_len = 0;
        }
    }
}

void gps_manager_deinit() {
    if (s_enabled) gpsSerial.end();
    s_enabled = s_has_fix = false;
}

bool   gps_manager_enabled()           { return s_enabled; }
bool   gps_manager_has_fix()           { return s_has_fix; }
double gps_manager_latitude()          { return s_lat; }
double gps_manager_longitude()         { return s_lon; }
float  gps_manager_altitude_m()        { return s_alt; }
float  gps_manager_speed_kmh()         { return s_speed; }
float  gps_manager_heading_deg()       { return s_heading; }
int    gps_manager_satellites_used()   { return s_sats_used; }
int    gps_manager_satellites_visible(){ return s_sats_visible; }
float  gps_manager_hdop()              { return s_hdop; }

void gps_manager_time(uint8_t* h, uint8_t* m, uint8_t* s) {
    *h = s_hour; *m = s_min; *s = s_sec;
}
void gps_manager_date(uint16_t* y, uint8_t* mo, uint8_t* d) {
    *y = s_year; *mo = s_month; *d = s_day;
}
const char* gps_manager_last_nmea() { return s_last_nmea; }
