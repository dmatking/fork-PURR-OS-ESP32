#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Radio: SX1276 / RFM95W — SPI direct, arduino-LoRa library
// Drop-in target: generic SX1276/RFM95W breakout boards wired to SPI.
// Copy this folder's lora_manager.h/.cpp into CoreOS/system/kernel/modules/ to activate.
// Requires: LoRa (arduino-LoRa) in CMakeLists REQUIRES.

// Adjust these pins to match your wiring.
#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_CS     5
#define LORA_RST   14
#define LORA_IRQ   26   // DIO0 on SX1276 (not DIO1 like SX1262)

void   lora_manager_init(uint32_t freq_hz, uint8_t power_dbm);
void   lora_manager_tick();
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
