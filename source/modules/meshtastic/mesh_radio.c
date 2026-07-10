// mesh_radio.c — Meshtastic LONG_FAST radio preset + AES-128-CTR crypto.
// Ported from PURR-OS-0.11/CoreOS/system/kernel/modules/purr_mesh.cpp — the
// only real change is that this talks to the radio through
// purr_kernel_radio()'s catcall_radio_t instead of a bespoke lora_manager
// singleton, so it's radio-chip-agnostic (works with sx1262 or sx1276).

#include "sdkconfig.h"

// No external caller besides meshtastic_module.c — see mesh_router.c's
// matching comment. Empty translation unit when the feature is off.
#ifdef CONFIG_PURR_FEATURE_MESHTASTIC

#include "mesh_radio.h"
#include "../../kernel/core/purr_kernel.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <mbedtls/aes.h>
#include <string.h>
#include <stdio.h>

uint32_t mesh_radio_freq_for_region(const char *region)
{
    if (region && strcmp(region, "EU") == 0) return 869525000UL;
    if (region && strcmp(region, "JP") == 0) return 923875000UL;
    if (region && strcmp(region, "CN") == 0) return 470000000UL;
    if (region && strcmp(region, "AU") == 0) return 916875000UL;
    return 906875000UL;  // US default (also ANZ, TW, KR approximate)
}

// See mesh_radio_lock()'s doc comment in mesh_radio.h for why this exists.
static SemaphoreHandle_t s_radio_mutex = NULL;

void mesh_radio_lock(void)
{
    if (s_radio_mutex) xSemaphoreTake(s_radio_mutex, portMAX_DELAY);
}

void mesh_radio_unlock(void)
{
    if (s_radio_mutex) xSemaphoreGive(s_radio_mutex);
}

// ── Channels ─────────────────────────────────────────────────────────────────

#define MESH_NVS_NS "purr_mesh"

// Default channel PSK (Meshtastic "1" shorthand — the real 16-byte key used
// as-is). Meshtastic's own Channels.h calls this out explicitly: "16 bytes
// of random PSK for our _public_ default channel that all devices power up
// on (AES128)" — this was previously doubled into a fake 32-byte key and
// run through AES-256, which silently produced a completely different
// keystream from every real Meshtastic node (same bug on both encode and
// decode, so two PURR nodes could still talk to each other — just never to
// anything real).
static const uint8_t DEFAULT_PSK16[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

static mesh_channel_t s_channels[MESH_MAX_CHANNELS];
static int            s_channel_count = 0;

uint8_t mesh_radio_channel_hash(const char *name, const uint8_t psk[16])
{
    uint8_t code = 0;
    for (size_t i = 0; name[i]; i++) code ^= (uint8_t)name[i];
    uint8_t psk_code = 0;
    for (size_t i = 0; i < 16; i++) psk_code ^= psk[i];
    return code ^ psk_code;
}

int mesh_radio_channel_count(void) { return s_channel_count; }

const mesh_channel_t *mesh_radio_channel_at(int idx)
{
    if (idx < 0 || idx >= s_channel_count) return NULL;
    return &s_channels[idx];
}

int mesh_radio_channel_find_by_hash(uint8_t hash)
{
    for (int i = 0; i < s_channel_count; i++) {
        if (s_channels[i].active && s_channels[i].hash == hash) return i;
    }
    return -1;
}

static void save_custom_channels(void)
{
    nvs_handle_t h;
    if (nvs_open(MESH_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    // Channel 0 (LongFast) is never persisted — it's re-derived identically
    // every boot in mesh_radio_apply_preset(), only slots 1..count-1 (the
    // user-added ones) need saving.
    int custom_count = s_channel_count - 1;
    nvs_set_blob(h, "channels", &s_channels[1], sizeof(mesh_channel_t) * (size_t)custom_count);
    nvs_set_i32(h, "count", custom_count);
    nvs_commit(h);
    nvs_close(h);
}

static void load_custom_channels(void)
{
    nvs_handle_t h;
    if (nvs_open(MESH_NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    int32_t custom_count = 0;
    nvs_get_i32(h, "count", &custom_count);
    if (custom_count > 0) {
        if (custom_count > MESH_MAX_CHANNELS - 1) custom_count = MESH_MAX_CHANNELS - 1;
        size_t blob_len = sizeof(mesh_channel_t) * (size_t)custom_count;
        if (nvs_get_blob(h, "channels", &s_channels[1], &blob_len) == ESP_OK) {
            s_channel_count += custom_count;
        }
    }
    nvs_close(h);
}

int mesh_radio_add_channel(const char *name, const uint8_t psk[16])
{
    if (s_channel_count >= MESH_MAX_CHANNELS) return -1;
    uint8_t hash = mesh_radio_channel_hash(name, psk);
    if (mesh_radio_channel_find_by_hash(hash) >= 0) return -1;  // hash collision

    mesh_channel_t *c = &s_channels[s_channel_count];
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, sizeof(c->name) - 1);
    memcpy(c->psk, psk, 16);
    c->hash   = hash;
    c->active = true;
    int idx = s_channel_count++;
    save_custom_channels();
    return idx;
}

bool mesh_radio_apply_preset(uint32_t freq_hz)
{
    const catcall_radio_t *radio = purr_kernel_radio();
    if (!radio) return false;

    // Created here rather than at module-load time — this is the one
    // point every caller's startup already funnels through before
    // s_ready lets mesh_manager_send_text() run, so there's no window
    // where a caller could see s_radio_mutex still NULL after the mesh
    // is reported ready. Same reasoning for the channel table below: this
    // is the one guaranteed-once, pre-s_ready initialization point.
    if (!s_radio_mutex) s_radio_mutex = xSemaphoreCreateMutex();

    if (s_channel_count == 0) {
        mesh_channel_t *primary = &s_channels[0];
        snprintf(primary->name, sizeof(primary->name), "LongFast");
        memcpy(primary->psk, DEFAULT_PSK16, 16);
        primary->hash   = mesh_radio_channel_hash(primary->name, primary->psk);
        primary->active = true;
        s_channel_count = 1;
        load_custom_channels();
    }

    if (radio->set_modulation) radio->set_modulation(MESH_SF, MESH_BW_HZ, MESH_CR);
    if (radio->set_sync_word)  radio->set_sync_word(MESH_SYNC_WORD);
    if (radio->set_frequency)  radio->set_frequency(freq_hz);
    if (radio->set_power)      radio->set_power(MESH_TX_DBM);
    return true;
}

void mesh_radio_build_iv(uint8_t iv[16], uint32_t pkt_id, uint32_t from_node)
{
    memset(iv, 0, 16);
    uint64_t id64 = pkt_id;
    memcpy(iv, &id64, 8);
    memcpy(iv + 8, &from_node, 4);
}

bool mesh_radio_aes_ctr(const uint8_t *in, uint8_t *out, size_t len,
                         const uint8_t key16[16], const uint8_t iv[16])
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    // AES-128, not AES-256 — see mesh_radio_default_psk()'s comment. The
    // channel's actual key length (16 bytes) is what selects the cipher in
    // real Meshtastic firmware (CryptoEngine::encryptAESCtr: 16 bytes ==
    // AES128, else AES256); this project only ever uses the 16-byte default
    // channel key today, so AES-128 is simply the correct, only case.
    if (mbedtls_aes_setkey_enc(&ctx, key16, 128) != 0) {
        mbedtls_aes_free(&ctx);
        return false;
    }
    uint8_t counter[16], stream[16];
    memcpy(counter, iv, 16);
    size_t nc_off = 0;
    int rc = mbedtls_aes_crypt_ctr(&ctx, len, &nc_off, counter, stream,
                                    (const uint8_t *)in, out);
    mbedtls_aes_free(&ctx);
    return rc == 0;
}

#endif  // CONFIG_PURR_FEATURE_MESHTASTIC
