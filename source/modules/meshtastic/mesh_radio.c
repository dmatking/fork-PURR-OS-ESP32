// mesh_radio.c — Meshtastic LONG_FAST radio preset + AES-256-CTR crypto.
// Ported from PURR-OS-0.11/CoreOS/system/kernel/modules/purr_mesh.cpp — the
// only real change is that this talks to the radio through
// purr_kernel_radio()'s catcall_radio_t instead of a bespoke lora_manager
// singleton, so it's radio-chip-agnostic (works with sx1262 or sx1276).

#include "mesh_radio.h"
#include "../../kernel/core/purr_kernel.h"
#include <mbedtls/aes.h>
#include <string.h>

uint32_t mesh_radio_freq_for_region(const char *region)
{
    if (region && strcmp(region, "EU") == 0) return 869525000UL;
    if (region && strcmp(region, "JP") == 0) return 923875000UL;
    if (region && strcmp(region, "CN") == 0) return 470000000UL;
    if (region && strcmp(region, "AU") == 0) return 916875000UL;
    return 906875000UL;  // US default (also ANZ, TW, KR approximate)
}

bool mesh_radio_apply_preset(uint32_t freq_hz)
{
    const catcall_radio_t *radio = purr_kernel_radio();
    if (!radio) return false;

    if (radio->set_modulation) radio->set_modulation(MESH_SF, MESH_BW_HZ, MESH_CR);
    if (radio->set_sync_word)  radio->set_sync_word(MESH_SYNC_WORD);
    if (radio->set_frequency)  radio->set_frequency(freq_hz);
    if (radio->set_power)      radio->set_power(MESH_TX_DBM);
    return true;
}

// Default channel PSK (Meshtastic "1" shorthand → 16-byte key, doubled to 32).
static const uint8_t DEFAULT_PSK16[16] = {
    0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
    0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

void mesh_radio_expand_psk(uint8_t key32[32])
{
    memcpy(key32,      DEFAULT_PSK16, 16);
    memcpy(key32 + 16, DEFAULT_PSK16, 16);
}

void mesh_radio_build_iv(uint8_t iv[16], uint32_t pkt_id, uint32_t from_node)
{
    memset(iv, 0, 16);
    uint64_t id64 = pkt_id;
    memcpy(iv, &id64, 8);
    memcpy(iv + 8, &from_node, 4);
}

bool mesh_radio_aes_ctr(const uint8_t *in, uint8_t *out, size_t len,
                         const uint8_t key32[32], const uint8_t iv[16])
{
    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    if (mbedtls_aes_setkey_enc(&ctx, key32, 256) != 0) {
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
