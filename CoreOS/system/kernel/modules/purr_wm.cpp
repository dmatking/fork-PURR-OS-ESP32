// purr_wm.cpp — PURR OS Window Manager
// Thin shell management layer on top of LVGL.

#ifdef PURR_HAS_LVGL

#include "purr_wm.h"
#include "lua_runtime.h"
#include <lvgl.h>
#include "../purr_idf_compat.h"
#include <string.h>

// ── Shell registry ─────────────────────────────────────────────────────────
static purr_shell_create_fn s_shell_fns[PURR_SHELL_COUNT] = {};
static purr_shell_t         s_active_shell = PURR_SHELL_BLACKBERRY;
static lv_obj_t*            s_shell_screen = nullptr;

// ── Screen stack (for app layering) ───────────────────────────────────────
#define WM_STACK_MAX 8
static lv_obj_t* s_screen_stack[WM_STACK_MAX];
static int        s_stack_top = 0;

// ── Init ──────────────────────────────────────────────────────────────────

void purr_wm_init() {
    memset(s_shell_fns, 0, sizeof(s_shell_fns));
    memset(s_screen_stack, 0, sizeof(s_screen_stack));
    s_stack_top = 0;
    Serial.println("[wm] initialized");
}

// ── Shell registration ────────────────────────────────────────────────────

void purr_wm_register_shell(purr_shell_t id, purr_shell_create_fn fn) {
    if (id < PURR_SHELL_COUNT) {
        s_shell_fns[id] = fn;
        Serial.printf("[wm] shell %d registered\n", id);
    }
}

// ── Shell start ───────────────────────────────────────────────────────────

extern "C" void purr_wm_start() {
    if (!s_shell_fns[s_active_shell]) {
        Serial.printf("[wm] ERROR: no shell registered for id %d\n", s_active_shell);
        return;
    }

    lv_obj_t* scr = lv_scr_act();
    s_shell_screen = s_shell_fns[s_active_shell](scr);
    s_screen_stack[0] = scr;
    s_stack_top = 1;

    Serial.printf("[wm] shell %d started\n", s_active_shell);
}

// ── Shell switching ───────────────────────────────────────────────────────

void purr_wm_switch_shell(purr_shell_t shell) {
    if (shell >= PURR_SHELL_COUNT || !s_shell_fns[shell]) {
        Serial.printf("[wm] switch failed — shell %d not registered\n", shell);
        return;
    }

    // Create new screen for incoming shell
    lv_obj_t* new_scr = lv_obj_create(nullptr);
    s_shell_fns[shell](new_scr);

    lv_scr_load_anim(new_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, true);

    s_active_shell   = shell;
    s_shell_screen   = new_scr;
    s_screen_stack[0] = new_scr;
    s_stack_top       = 1;

    Serial.printf("[wm] switched to shell %d\n", shell);
}

purr_shell_t purr_wm_active_shell() {
    return s_active_shell;
}

// ── App lifecycle ─────────────────────────────────────────────────────────

// Detect runtime from file magic bytes
static bool is_lua_file(const char* path) {
    // Check extension first
    const char* ext = strrchr(path, '.');
    if (ext && (strcmp(ext, ".lua") == 0 || strcmp(ext, ".luac") == 0)) return true;

    // Check magic bytes: Lua bytecode = 0x1B 'L' 'u' 'a'
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t magic[4] = {};
    fread(magic, 1, 4, f);
    fclose(f);
    return magic[0] == 0x1B && magic[1] == 'L' && magic[2] == 'u' && magic[3] == 'a';
}

static bool is_wasm_file(const char* path) {
    const char* ext = strrchr(path, '.');
    if (ext && strcmp(ext, ".wasm") == 0) return true;

    FILE* f = fopen(path, "rb");
    if (!f) return false;
    uint8_t magic[4] = {};
    fread(magic, 1, 4, f);
    fclose(f);
    // WASM magic: 0x00 'a' 's' 'm'
    return magic[0] == 0x00 && magic[1] == 'a' && magic[2] == 's' && magic[3] == 'm';
}

static bool is_restricted(const char* path) {
    const char* ext = strrchr(path, '.');
    return ext && strcmp(ext, ".paws") == 0;
}

bool purr_wm_launch(const char* path) {
    if (!path) return false;

    Serial.printf("[wm] launch %s\n", path);

    bool restricted = is_restricted(path);

    if (is_lua_file(path)) {
        return lua_run_file(path, restricted);
    }

    if (is_wasm_file(path)) {
        Serial.println("[wm] WASM runtime not yet implemented");
        return false;
    }

    // Try Lua as fallback for .paws/.claw
    return lua_run_file(path, restricted);
}

// ── Back (pop screen) ─────────────────────────────────────────────────────

void purr_wm_back() {
    if (s_stack_top <= 1) {
        Serial.println("[wm] already at root screen");
        return;
    }

    s_stack_top--;
    lv_obj_t* prev = s_screen_stack[s_stack_top - 1];
    lv_scr_load_anim(prev, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, true);
    Serial.printf("[wm] back — stack depth %d\n", s_stack_top);
}

// ── Notifications ─────────────────────────────────────────────────────────

void purr_wm_notify(const char* message, uint32_t duration_ms) {
    lv_obj_t* scr = lv_scr_act();

    // Toast container at bottom of screen
    lv_obj_t* toast = lv_obj_create(scr);
    lv_obj_set_size(toast, 280, 36);
    lv_obj_align(toast, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(toast, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(toast, LV_OPA_90, 0);
    lv_obj_set_style_radius(toast, 8, 0);
    lv_obj_set_style_border_width(toast, 0, 0);

    lv_obj_t* lbl = lv_label_create(toast);
    lv_label_set_text(lbl, message);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);

    // Auto-delete after duration
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
    });
    lv_anim_set_var(&a, toast);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, 400);
    lv_anim_set_delay(&a, duration_ms);
    lv_anim_set_deleted_cb(&a, [](lv_anim_t* a) {
        lv_obj_del((lv_obj_t*)a->var);
    });
    lv_anim_start(&a);

    Serial.printf("[wm] notify: %s (%lums)\n", message, (unsigned long)duration_ms);
}

#endif  // PURR_HAS_LVGL
