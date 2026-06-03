#pragma once
// u-blox MIA-M10Q GNSS — T-Deck Plus only (PURR_IS_TDECK_PLUS)
//
// Hardware: occupies the former Grove interface (GPIO43 TX / GPIO44 RX).
// The Grove port is unavailable on T-Deck Plus.
//
// UART: Serial1, GPIO44 = RX (from module), GPIO43 = TX (to module)
// Baud: probed at 9600 then 4800 on init; u-blox M10 defaults to 9600.
//
// Detection note: the MIA-M10Q needs ~1-2 s after power-on before NMEA
// sentences begin. Probing too quickly causes false "no GPS" results
// (observed in Meshtastic firmware). Init sends a short wait before
// declaring the module absent.

#include <stdint.h>
#include <stdbool.h>

#define GPS_UART_RX_PIN  44
#define GPS_UART_TX_PIN  43
#define GPS_UART_BAUD_DEFAULT  9600
#define GPS_UART_BAUD_FALLBACK 4800
#define GPS_INIT_WAIT_MS       2000   // wait after power-on before probing

void gps_manager_init();
void gps_manager_update();   // call from KITT update loop; feeds NMEA parser
void gps_manager_deinit();

bool gps_manager_enabled();  // true once module responded to probe
bool gps_manager_has_fix();  // true once GPRMC/GNRMC reports active fix

// Position (valid only when has_fix())
double gps_manager_latitude();    // decimal degrees, positive = N
double gps_manager_longitude();   // decimal degrees, positive = E
float  gps_manager_altitude_m();

// Motion
float  gps_manager_speed_kmh();
float  gps_manager_heading_deg();

// Signal quality
int    gps_manager_satellites_used();
int    gps_manager_satellites_visible();
float  gps_manager_hdop();

// UTC time (valid when has_fix())
void   gps_manager_time(uint8_t* hour, uint8_t* min, uint8_t* sec);
void   gps_manager_date(uint16_t* year, uint8_t* month, uint8_t* day);

// Raw last NMEA sentence received (null-terminated)
const char* gps_manager_last_nmea();
