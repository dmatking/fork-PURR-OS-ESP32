#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Radio: RAK3172 (STM32WL) — UART AT commands, LoRa P2P mode
// Drop-in target: boards where the LoRa module is connected via UART (not SPI)
// Copy this folder's lora_manager.h/.cpp into CoreOS/system/kernel/modules/ to activate.

// TODO: verify TX/RX pin numbers against PCB once board is in hand.
#define LORA_UART_TX   -1   // TBD
#define LORA_UART_RX   -1   // TBD
#define LORA_UART_BAUD 115200

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
