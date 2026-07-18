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
// Multi-device trust list — up to PAIRING_MAX_DEVICES remembered pairings,
// persisted (see pairing_module.c's mesh_router.c-style NVS blob pattern).
// The negotiation itself (REQUEST/ACCEPT/REJECT handshake, PENDING_* states
// below) is still only ever one-at-a-time — you can't have two pairing
// requests in flight simultaneously — but a successfully confirmed pairing
// is now APPENDED to the trust list rather than overwriting a single slot.
// Security (encryption/authentication of the link) is explicitly out of
// scope this pass; the pairing code only confirms *which physical device*
// you're pairing with (protects against drive-by pairing with a stranger's
// device merely by being in range), not confidentiality or integrity of
// the resulting link — every higher-level feature built on this trust list
// (e.g. the Remote Apps RPC layer) must re-check pairing_is_trusted() on
// every inbound frame itself; this module doesn't gate anything but its
// own handshake traffic.

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

#define PAIRING_MAX_DEVICES 8

typedef struct {
    uint8_t mac[6];
    char    name[20];
} paired_device_t;

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
//
// pairing_is_paired()/pairing_get_paired_mac()/pairing_get_paired_name()
// operate on trust-list index 0 (the first/oldest remembered device) —
// kept for the two existing single-device callers (nearby_app.c,
// oled_ui_module.c), neither of which needs to distinguish between
// multiple paired devices. New code that needs the full list uses the
// indexed API below instead.
bool pairing_is_paired(void);
bool pairing_get_paired_mac(uint8_t out_mac[6]);
bool pairing_get_paired_name(char *out, size_t out_len);
// Clears trust-list index 0 (the first/oldest paired device) and
// (best-effort) notifies the peer. Safe to call regardless of current
// state. New code should prefer pairing_forget(mac) instead, which is
// unambiguous about which device.
void pairing_unpair(void);

// ── Multi-device trust list ─────────────────────────────────────────────
int  pairing_device_count(void);
// Fills *out for a trust-list index. Returns false if idx is out of range.
bool pairing_device_at(int idx, paired_device_t *out);
// True if mac is in the trust list — the authorization check every
// higher-level feature (Remote Apps RPC, etc.) must apply to inbound
// frames itself; see this header's own top comment.
bool pairing_is_trusted(const uint8_t mac[6]);
// Removes mac from the trust list (best-effort UNPAIR notice to the peer,
// same as pairing_unpair()) if present. No-op otherwise.
void pairing_forget(const uint8_t mac[6]);

// ── Home base ────────────────────────────────────────────────────────────
// At most one paired device can be designated "home base" — the target of
// MSN's automatic radio-relay handoff (see source/modules/homebase/). Kept
// as its own NVS key rather than a field on paired_device_t: growing that
// struct would change its NVS blob size with no format-version key to
// detect old blobs, silently dropping every existing pairing on upgrade
// (see pairing_module.c's load_paired()/save_paired()). Storing this
// separately avoids that risk entirely.
bool pairing_set_home_base(const uint8_t mac[6]);   // false if mac isn't in the trust list
bool pairing_get_home_base(uint8_t out_mac[6]);      // false if none set
bool pairing_is_home_base(const uint8_t mac[6]);
void pairing_clear_home_base(void);

#ifdef __cplusplus
}
#endif
