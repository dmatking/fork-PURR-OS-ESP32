#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// CC1101 sub-GHz FSK radio driver (RadioLib)
// T-Embed CC1101 default wiring (SPI2):
//   MOSI=6  MISO=5  SCK=7  CS=4  GDO0=3  GDO2=8

#define CC1101_SPI_HOST  SPI2_HOST
#define CC1101_MOSI      6
#define CC1101_MISO      5
#define CC1101_SCK       7
#define CC1101_CS        4
#define CC1101_GDO0      3
#define CC1101_GDO2      8

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
