#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// CC1101 sub-GHz FSK radio driver (RadioLib)
// T-Embed CC1101 verified wiring (SPI2, shared with display):
//   MOSI=9  MISO=10  SCK=11  CS=12  GDO0=38  GDO2=39
// Override with PURR_CC1101_* compile definitions if needed.

#define CC1101_SPI_HOST  1      // SPI2_HOST

#ifdef PURR_CC1101_MOSI
#  define CC1101_MOSI    PURR_CC1101_MOSI
#else
#  define CC1101_MOSI    9
#endif
#ifdef PURR_CC1101_MISO
#  define CC1101_MISO    PURR_CC1101_MISO
#else
#  define CC1101_MISO    10
#endif
#ifdef PURR_CC1101_SCK
#  define CC1101_SCK     PURR_CC1101_SCK
#else
#  define CC1101_SCK     11
#endif
#ifdef PURR_CC1101_CS
#  define CC1101_CS      PURR_CC1101_CS
#else
#  define CC1101_CS      12
#endif
#ifdef PURR_CC1101_GDO0
#  define CC1101_GDO0    PURR_CC1101_GDO0
#else
#  define CC1101_GDO0    38
#endif
#ifdef PURR_CC1101_GDO2
#  define CC1101_GDO2    PURR_CC1101_GDO2
#else
#  define CC1101_GDO2    39
#endif

// Default: 433.92 MHz, 4.8 kbps FSK (POCSAG-compatible bandwidth)
void   cc1101_manager_init(float freq_mhz, float bitrate_kbps, float freq_dev_khz,
                            float rx_bw_khz, int8_t power_dbm);
void   cc1101_manager_deinit();
void   cc1101_manager_update();

bool     cc1101_manager_enabled();
void     cc1101_manager_set_frequency(float freq_mhz);
void     cc1101_manager_set_power(int8_t dbm);
float    cc1101_manager_get_frequency();
int8_t   cc1101_manager_get_power();
int      cc1101_manager_rssi();

bool     cc1101_manager_send(const uint8_t* data, size_t len);
bool     cc1101_manager_busy();
bool     cc1101_manager_data_available();
size_t   cc1101_manager_read(uint8_t* buf, size_t max_len);
void     cc1101_manager_yield();
void     cc1101_manager_reclaim();
bool     cc1101_manager_yielded();
