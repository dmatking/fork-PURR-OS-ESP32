#pragma once
// pairing.h — public API for PURR OS device pairing over ESP-NOW.
//
// General-purpose "trust this specific nearby device" mechanism — first
// use case is pairing a full-UI device (e.g. T-Deck Plus) with a headless
// LoRa "radio companion" (e.g. Heltec V3) so MSN can relay its mesh
// traffic through the companion's radio (see the "Remote radio companion"
// plan), but nothing here is Heltec-specific: either side can be the
// initiator or the responder, and any device with proximity_module.c's
// ESP-NOW subsystem can use this.
//
// Rides on proximity_module.c's shared ESP-NOW dispatch (PROXIMITY_FRAME_
// PAIRING) rather than owning its own radio callback — see proximity.h's
// own header comment for why that has to be true (ESP-NOW has one global
// recv-callback slot).
//
// Only ONE active pairing at a time, matching the roadmap's own scope
// ("both sides remember the paired MAC") — not a multi-device trust list.
// Security (encryption/authentication of the link) is explicitly out of
// scope this pass; the pairing code only confirms *which physical device*
// you're pairing with (protects against drive-by pairing with a stranger's
// device merely by being in range), not confidentiality or integrity of
// the resulting link.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  pairing_init(void);
void pairing_deinit(void);

typedef enum {
    PAIRING_STATE_NONE = 0,
    PAIRING_STATE_PENDING_OUTGOING,   // we initiated, waiting for their confirm
    PAIRING_STATE_PENDING_INCOMING,   // they initiated, waiting on our local confirm
    PAIRING_STATE_PAIRED,
} pairing_state_t;

pairing_state_t pairing_get_state(void);

// ── Initiator side ──────────────────────────────────────────────────────
// e.g. nearby_app.c on a press-and-hold of a device flagged
// PROXIMITY_CAP_RADIO_COMPANION. Fails (returns false) if a pairing is
// already active (PAIRED or PENDING_*) — unpair/cancel first.
bool pairing_start(const uint8_t mac[6], const char *peer_name);
// Cancels our own outgoing pending request (no-op if not PENDING_OUTGOING).
void pairing_cancel(void);

// ── Responder side ───────────────────────────────────────────────────────
// e.g. a new oled_ui screen on Heltec, or T-Deck Plus's own nearby_app.c —
// the mechanism is symmetric, either device can be asked to confirm an
// incoming request. Poll pairing_get_state() == PAIRING_STATE_PENDING_
// INCOMING, then use these to render a confirm screen.
bool pairing_get_pending_code(char *out, size_t out_len);        // e.g. "4821"
bool pairing_get_pending_peer_name(char *out, size_t out_len);
void pairing_confirm(void);   // local user accepted
void pairing_reject(void);    // local user declined

// ── Durable pairing state ───────────────────────────────────────────────
// Either side of an established pairing uses these — this is what msn.c's
// "Remote" backend (Phase C) checks before offering itself as a chooser
// option.
bool pairing_is_paired(void);
bool pairing_get_paired_mac(uint8_t out_mac[6]);
bool pairing_get_paired_name(char *out, size_t out_len);
// Clears the current pairing and (best-effort) notifies the peer. Safe to
// call regardless of current state.
void pairing_unpair(void);

#ifdef __cplusplus
}
#endif
