#pragma once
// app_manager_remote.h — Remote Apps (Milkbar) protocol shared between the
// responder (app_manager_remote.c, runs on the device being controlled)
// and any caller (Milkbar's UI, runs on the controlling device) via
// proximity_rpc_call(). See app_manager_remote.c's own top comment.

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REMOTEAPPS_ACTION_LIST   0x1000
#define REMOTEAPPS_ACTION_LAUNCH 0x1001
#define REMOTEAPPS_ACTION_STOP   0x1002

// One entry per app in a LIST response. tier/state are app_tier_t/
// app_state_t's raw values (app_manager.h) — Milkbar interprets them the
// same way the local Cat Apps launcher does.
typedef struct __attribute__((packed)) {
    char    name[48];
    uint8_t tier;
    uint8_t state;
} remote_app_entry_t;

// Called once from app_manager_init() to register this device as a Remote
// Apps responder. A no-op from the caller's perspective if proximity_rpc
// never actually receives a request — this just makes the device answer
// if one arrives from a trusted peer.
void app_manager_remote_register(void);

#ifdef __cplusplus
}
#endif
