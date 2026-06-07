#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Radio: SX1262 — SPI direct, arduino-LoRa library
// Drop-in target: Heltec V3 and boards with SX1262 wired to SPI.
// Copy this folder's lora_manager.h/.cpp into CoreOS/system/kernel/modules/ to activate.
// Requires: LoRa (arduino-LoRa or compatible SX1262 wrapper) in CMakeLists REQUIRES.

// Heltec V3 SX1262 SPI pins
#define LORA_SCK   9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS    8
#define LORA_RST  12
#define LORA_DIO1 14
#define LORA_BUSY 13

void   lora_manager_init(uint32_t freq_hz, uint8_t power_dbm);
void   lora_manager_update();
void   lora_manager_deinit();

bool     lora_manager_enabled();
void     lora_manager_set_frequency(uint32_t freq_hz);
void     lora_manager_set_power(uint8_t dbm);
void     lora_manager_set_spreading_factor(uint8_t sf);
void     lora_manager_set_bandwidth(uint32_t bw_hz);
void     lora_manager_set_coding_rate(uint8_t cr);
void     lora_manager_set_sync_word(uint8_t sw);
uint32_t lora_manager_get_frequency();
uint8_t  lora_manager_get_power();
int      lora_manager_rssi();
float    lora_manager_snr();
bool     lora_manager_send(const uint8_t* data, size_t len);
bool     lora_manager_busy();
bool     lora_manager_data_available();
size_t   lora_manager_read(uint8_t* buf, size_t max_len);
void     lora_manager_yield();
void     lora_manager_reclaim();
bool     lora_manager_yielded();
