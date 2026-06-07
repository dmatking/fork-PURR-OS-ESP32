// purr_bootloader.cpp — PURR OS recovery / OTA bootloader shell.
// Runs from the factory partition. Scans all OTA slots generically and lets
// the user Boot, Wipe, or Install firmware into any slot.
// Entered automatically when the factory partition boots (PURR_IS_BOOTLOADER_IMG).
// The full OS (ota_0) triggers a reboot to factory via pm_boot_to_factory().

#ifdef PURR_HAS_BOOTLOADER

#include "purr_bootloader.h"
#include "../purr_version.h"
#include "display_ili9341.h"
#include "touch_cst816s.h"
#include "partition_manager.h"
#include "../kitt.h"
#include "../purr_idf_compat.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_app_desc.h"
#include <esp_app_format.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

extern KITT kitt;

// ── Colour palette ────────────────────────────────────────────────────────────
static const uint16_t COL_BG     = 0x0883;
static const uint16_t COL_CARD   = 0x10A3;
static const uint16_t COL_ACCENT = 0x3CDF;
static const uint16_t COL_TEXT   = 0xFFFF;
static const uint16_t COL_SUB    = 0x8C51;
static const uint16_t COL_OK     = 0x2E48;
static const uint16_t COL_DANGER = 0xF186;
static const uint16_t COL_WARN   = 0xF400;
static const uint16_t COL_DIV    = 0x1906;
static const uint16_t COL_BTN    = 0x2965;
static const uint16_t COL_ACTIVE = 0x07E0;  // green badge — slot is active boot target

// ── Layout ────────────────────────────────────────────────────────────────────
#define SCR_W    320
#define SCR_H    240
#define HDR_H    26
#define FTR_H    27
#define SLOT_Y0  30
#define SLOT_H   88
#define SLOT_GAP 4
#define BTN_X    218
#define BTN_W    94
#define BTN_H    26
#define MAX_VISIBLE_SLOTS 2

// ── Button tag encoding ───────────────────────────────────────────────────────
// Upper nibble = action, lower nibble = slot index (0..15)
#define TAG_BOOT(s)    (0x10 | ((s) & 0x0F))
#define TAG_WIPE(s)    (0x20 | ((s) & 0x0F))
#define TAG_INSTALL(s) (0x30 | ((s) & 0x0F))
#define TAG_YES        0xE0
#define TAG_NO         0xE1
#define TAG_FILE(i)    (0xF0 | ((i) & 0x0F))

// ── Screen state ──────────────────────────────────────────────────────────────
enum PbScreen {
    PB_HOME,
    PB_CONFIRM_WIPE,
    PB_CONFIRM_BOOT,
    PB_CONFIRM_BACKUP,  // offer to dump PURR to SD before overwriting
    PB_BACKING_UP,      // progress screen while dumping
    PB_INSTALL_SELECT,
    PB_INSTALLING,
    PB_POST_INSTALL,    // after non-PURR flash: offer to restore PURR or boot new fw
    PB_RESTORING,       // progress screen while reflashing PURR from backup
    PB_SOS              // crash-loop recovery
};

static PbScreen         s_screen       = PB_HOME;
static uint8_t          s_boot_tries   = 0;
static int              s_confirm_slot = -1;
static int              s_install_slot = -1;
static pm_sd_file_t     s_files[PM_SD_MAX_FILES];
static int              s_nfiles       = 0;
static volatile int     s_install_pct  = 0;
static char             s_install_msg[64] = "";
static volatile bool    s_dirty        = true;
static bool             s_prev_pressed  = false;
static uint32_t         s_debounce_ms   = 0;

static struct { int slot; char path[PM_PATH_LEN]; char name[PM_NAME_LEN]; } s_inst;
static char             s_backup_path[PM_PATH_LEN] = "";  // SD path of PURR backup, or ""

// ── Draw helpers ──────────────────────────────────────────────────────────────
static void ps_fill(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
    display_ili9341_fill_rect(x, y, w, h, c);
}
static void ps_hline(int16_t x, int16_t y, int16_t w, uint16_t c) {
    display_ili9341_draw_hline(x, y, w, c);
}
static void ps_str(int16_t x, int16_t y, const char* s, uint16_t fg, uint16_t bg, uint8_t sz) {
    display_ili9341_draw_string(x, y, s, fg, bg, sz);
}
static void ps_strf(int16_t x, int16_t y, uint16_t fg, uint16_t bg, uint8_t sz,
                    const char* fmt, ...) {
    char buf[80]; va_list va; va_start(va, fmt); vsnprintf(buf, sizeof(buf), fmt, va); va_end(va);
    ps_str(x, y, buf, fg, bg, sz);
}

// ── Hit table ─────────────────────────────────────────────────────────────────
struct Btn { int16_t x, y, w, h; int tag; };
static Btn  s_btns[16];
static int  s_nbtn = 0;

static void ps_btn(int16_t x, int16_t y, int16_t w, int16_t h,
                   const char* label, uint16_t fg, uint16_t bg, int tag) {
    ps_fill(x, y, w, h, bg);
    int16_t tx = x + (w - (int16_t)(strlen(label) * 6)) / 2;
    int16_t ty = y + (h - 8) / 2;
    ps_str(tx, ty, label, fg, bg, 1);
    if (s_nbtn < 16) s_btns[s_nbtn++] = {x, y, w, h, tag};
}

static int hit_test(int16_t tx, int16_t ty) {
    for (int i = 0; i < s_nbtn; i++) {
        const Btn& b = s_btns[i];
        if (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h)
            return b.tag;
    }
    return -1;
}

// ── Header ────────────────────────────────────────────────────────────────────
static void render_header() {
    ps_fill(0, 0, SCR_W, HDR_H, COL_BG);
    ps_str(8, 5, "PURR OS v" PURR_OS_VERSION " Kernel", COL_ACCENT, COL_BG, 2);
    KITT::memory_stats_t mem; kitt.memory_get_stats(&mem);
    char right[40];
    snprintf(right, sizeof(right), "%s  %luK", kitt.device_name(),
             (unsigned long)mem.free_ram_kb);
    ps_str(SCR_W - (int16_t)(strlen(right) * 6) - 6, 9, right, COL_SUB, COL_BG, 1);
    ps_hline(0, HDR_H, SCR_W, COL_DIV);
}

static void refresh_header_ram() {
    KITT::memory_stats_t mem; kitt.memory_get_stats(&mem);
    char right[40];
    snprintf(right, sizeof(right), "%s  %luK", kitt.device_name(),
             (unsigned long)mem.free_ram_kb);
    ps_fill(130, 5, 186, 14, COL_BG);
    ps_str(SCR_W - (int16_t)(strlen(right) * 6) - 6, 9, right, COL_SUB, COL_BG, 1);
}

// ── Footer ────────────────────────────────────────────────────────────────────
static void render_footer() {
    ps_hline(0, SCR_H - FTR_H - 1, SCR_W, COL_DIV);
    ps_fill(0, SCR_H - FTR_H, SCR_W, FTR_H, COL_BG);
    ps_str(8, SCR_H - FTR_H + 9,
           pm_sd_available() ? "SD: ready" : "SD: none",
           pm_sd_available() ? COL_OK : COL_WARN, COL_BG, 1);
}

// ── Generic slot card ─────────────────────────────────────────────────────────
// is_active = this slot is currently set as the OTA boot target
static void render_slot_card(uint8_t slot, int16_t y, bool is_active) {
    pm_slot_t sl;
    bool ok = pm_slot_info(slot, &sl);

    ps_fill(4, y, SCR_W - 8, SLOT_H, COL_CARD);

    // Slot label + active badge
    char label[12];
    snprintf(label, sizeof(label), "OTA %u", slot);
    ps_str(12, y + 8, label, COL_SUB, COL_CARD, 1);
    if (is_active) {
        ps_str(12 + (int16_t)(strlen(label) * 6) + 6, y + 8,
               "ACTIVE", COL_ACTIVE, COL_CARD, 1);
    }

    if (!ok) {
        ps_str(12, y + 36, "ERR: slot unavailable", COL_DANGER, COL_CARD, 1);
        return;
    }

    if (sl.valid) {
        // Firmware name (title row)
        char name[20];
        strncpy(name, sl.name[0] ? sl.name : "(unnamed)", sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
        ps_str(12, y + 20, name, COL_TEXT, COL_CARD, 2);

        // Version from partition app_desc (reads directly from flash, not running image)
        const esp_partition_t* part = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot),
            NULL);
        esp_app_desc_t desc;
        if (part && esp_ota_get_partition_description(part, &desc) == ESP_OK) {
            ps_strf(12, y + 42, COL_SUB, COL_CARD, 1, "v%s  %.2f MB",
                    desc.version, sl.size_bytes / 1048576.0f);
        } else {
            ps_strf(12, y + 42, COL_SUB, COL_CARD, 1, "%.2f MB", sl.size_bytes / 1048576.0f);
        }
        ps_str(12, y + 54, sl.label, COL_ACCENT, COL_CARD, 1);

        // Buttons: BOOT (sets active + restart) | WIPE
        ps_btn(BTN_X,          y + 8,  BTN_W, BTN_H, "BOOT", COL_BG, COL_OK,     TAG_BOOT(slot));
        ps_btn(BTN_X,          y + 44, BTN_W, BTN_H, "WIPE", COL_BG, COL_DANGER, TAG_WIPE(slot));
    } else {
        ps_str(12, y + 22, "EMPTY", COL_SUB, COL_CARD, 2);
        ps_str(12, y + 44, "No firmware installed", COL_SUB, COL_CARD, 1);
        ps_btn(BTN_X, y + 30, BTN_W, BTN_H, "INSTALL", COL_TEXT, COL_ACCENT, TAG_INSTALL(slot));
    }
}

// ── Home screen ───────────────────────────────────────────────────────────────
static void render_home() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();

    int boot_slot = pm_boot_slot();
    uint8_t nslots = pm_slot_count();
    for (int i = 0; i < (int)nslots && i < MAX_VISIBLE_SLOTS; i++) {
        int16_t card_y = (int16_t)(SLOT_Y0 + i * (SLOT_H + SLOT_GAP));
        render_slot_card((uint8_t)i, card_y, boot_slot == i);
    }
    render_footer();
}

// ── Confirm overlays ──────────────────────────────────────────────────────────
static void render_confirm_wipe() {
    s_nbtn = 0;
    ps_fill(30, 40, 260, 155, COL_CARD);
    ps_hline(30, 40,  260, COL_DANGER);
    ps_hline(30, 194, 260, COL_DANGER);

    char title[24];
    snprintf(title, sizeof(title), "Wipe OTA %d?", s_confirm_slot);
    ps_str(42, 52, title, COL_TEXT, COL_CARD, 2);
    ps_str(42, 76,  "This erases the firmware and resets", COL_WARN, COL_CARD, 1);
    ps_str(42, 88,  "the boot target to factory.", COL_WARN, COL_CARD, 1);
    ps_str(42, 104, "Are you sure?", COL_DANGER, COL_CARD, 1);

    ps_btn(40,  160, 100, 28, "CANCEL", COL_TEXT, COL_BTN,    TAG_NO);
    ps_btn(180, 160, 100, 28, "WIPE",   COL_BG,   COL_DANGER, TAG_YES);
}

static void render_confirm_boot() {
    s_nbtn = 0;
    pm_slot_t sl;
    pm_slot_info((uint8_t)s_confirm_slot, &sl);

    ps_fill(50, 60, 220, 120, COL_CARD);
    ps_hline(50, 60,  220, COL_OK);
    ps_hline(50, 179, 220, COL_OK);

    char title[24];
    snprintf(title, sizeof(title), "Boot OTA %d?", s_confirm_slot);
    ps_str(62, 72, title, COL_TEXT, COL_CARD, 2);
    ps_str(62, 96, sl.name[0] ? sl.name : "(unnamed)", COL_SUB, COL_CARD, 1);
    ps_str(62, 110, "Device will restart.", COL_WARN, COL_CARD, 1);

    ps_btn(60,  142, 90, 28, "CANCEL", COL_TEXT, COL_BTN, TAG_NO);
    ps_btn(180, 142, 80, 28, "BOOT",   COL_BG,   COL_OK,  TAG_YES);
}

// ── SD file picker ────────────────────────────────────────────────────────────
static void render_install_select() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();

    char title[48];
    snprintf(title, sizeof(title), "Install to OTA %d — select .bin:", s_install_slot);
    ps_str(8, HDR_H + 6, title, COL_SUB, COL_BG, 1);
    ps_hline(0, HDR_H + 16, SCR_W, COL_DIV);

    if (!pm_sd_available()) {
        ps_str(8, HDR_H + 30, "No SD card detected.", COL_WARN, COL_BG, 1);
    } else if (s_nfiles == 0) {
        ps_str(8, HDR_H + 30, "No .bin files on SD root.", COL_WARN, COL_BG, 1);
    } else {
        for (int i = 0; i < s_nfiles && i < 4; i++) {
            int16_t fy = (int16_t)(HDR_H + 22 + i * 38);
            ps_fill(4, fy, SCR_W - 8, 34, COL_CARD);
            ps_str(10, fy + 6,  s_files[i].name, COL_TEXT, COL_CARD, 1);
            ps_strf(10, fy + 18, COL_SUB, COL_CARD, 1,
                    "%.2f MB", s_files[i].size_bytes / 1048576.0f);
            ps_btn(SCR_W - 90, fy + 4, 80, 26, "SELECT", COL_BG, COL_ACCENT, TAG_FILE(i));
        }
    }

    ps_hline(0, SCR_H - FTR_H - 1, SCR_W, COL_DIV);
    ps_btn(8, SCR_H - FTR_H + 4, 60, 20, "BACK", COL_TEXT, COL_BTN, TAG_NO);
}

// ── Install progress ──────────────────────────────────────────────────────────
static void render_installing() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();
    ps_strf(8, HDR_H + 22, COL_TEXT, COL_BG, 2, "Installing...");
    ps_fill(8, HDR_H + 52, SCR_W - 16, 22, COL_DIV);
    int bar = (int)((SCR_W - 16) * s_install_pct / 100);
    if (bar > 0) ps_fill(8, HDR_H + 52, bar, 22, COL_ACCENT);
    char pct[8]; snprintf(pct, sizeof(pct), "%d%%", s_install_pct);
    ps_str(SCR_W / 2 - 12, HDR_H + 56, pct, COL_TEXT,
           s_install_pct > 30 ? COL_ACCENT : COL_DIV, 1);
    ps_str(8, HDR_H + 84, s_install_msg, COL_SUB, COL_BG, 1);
}

// ── Confirm backup before overwriting PURR slot ───────────────────────────────
static void render_confirm_backup() {
    s_nbtn = 0;
    ps_fill(20, 30, 280, 180, COL_CARD);
    ps_hline(20, 30,  280, COL_ACCENT);
    ps_hline(20, 209, 280, COL_ACCENT);

    ps_str(32, 42, "Back up PURR OS first?", COL_TEXT, COL_CARD, 2);
    ps_str(32, 66, "OTA slot has PURR firmware.", COL_SUB, COL_CARD, 1);
    ps_str(32, 80, "Save it to SD before overwriting", COL_SUB, COL_CARD, 1);
    ps_str(32, 94, "so you can restore it later.", COL_SUB, COL_CARD, 1);

    char dst[PM_PATH_LEN];
    snprintf(dst, sizeof(dst), "→ /PURR_BACKUP_OTA%d.bin", s_install_slot);
    ps_str(32, 112, dst, COL_ACCENT, COL_CARD, 1);

    ps_btn(30,  165, 110, 30, "SKIP",   COL_TEXT, COL_BTN,    TAG_NO);
    ps_btn(160, 165, 110, 30, "BACKUP", COL_BG,   COL_ACCENT, TAG_YES);
}

// ── Backup progress ───────────────────────────────────────────────────────────
static void render_backing_up() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();
    ps_str(8, HDR_H + 22, "Backing up PURR OS...", COL_TEXT, COL_BG, 2);
    ps_fill(8, HDR_H + 52, SCR_W - 16, 22, COL_DIV);
    int bar = (int)((SCR_W - 16) * s_install_pct / 100);
    if (bar > 0) ps_fill(8, HDR_H + 52, bar, 22, COL_ACCENT);
    char pct[8]; snprintf(pct, sizeof(pct), "%d%%", s_install_pct);
    ps_str(SCR_W / 2 - 12, HDR_H + 56, pct, COL_TEXT,
           s_install_pct > 30 ? COL_ACCENT : COL_DIV, 1);
    ps_str(8, HDR_H + 84, s_install_msg, COL_SUB, COL_BG, 1);
    ps_str(8, HDR_H + 100, s_backup_path, COL_ACCENT, COL_BG, 1);
}

// ── Post-install: offer to restore PURR or boot new firmware ─────────────────
static void render_post_install() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    ps_fill(0, 0, SCR_W, HDR_H, COL_OK);
    ps_str(8, 7, "Install complete!", COL_BG, COL_OK, 1);

    ps_str(8, HDR_H + 12, "New firmware is ready in the slot.", COL_TEXT, COL_BG, 1);

    if (s_backup_path[0]) {
        ps_str(8, HDR_H + 28, "A PURR OS backup is on your SD card.", COL_SUB, COL_BG, 1);
        ps_str(8, HDR_H + 42, s_backup_path, COL_ACCENT, COL_BG, 1);
    }

    ps_hline(0, HDR_H + 58, SCR_W, COL_DIV);

    // RESTORE PURR
    if (s_backup_path[0]) {
        ps_btn(8,   HDR_H + 68, 150, 32, "RESTORE PURR", COL_BG, COL_ACCENT, TAG_YES);
        ps_str(166, HDR_H + 76, "Reflash PURR from backup", COL_SUB, COL_BG, 1);
    }

    // BOOT NEW FW
    ps_btn(8,   HDR_H + 110, 150, 32, "BOOT NEW FW",  COL_TEXT, COL_BTN,    TAG_NO);
    ps_str(166, HDR_H + 118, "Boot installed firmware", COL_WARN, COL_BG, 1);
    ps_str(166, HDR_H + 130, "(PURR Kernel stays safe)", COL_SUB, COL_BG, 1);
}

// ── Restore progress ──────────────────────────────────────────────────────────
static void render_restoring() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);
    render_header();
    ps_str(8, HDR_H + 22, "Restoring PURR OS...", COL_TEXT, COL_BG, 2);
    ps_fill(8, HDR_H + 52, SCR_W - 16, 22, COL_DIV);
    int bar = (int)((SCR_W - 16) * s_install_pct / 100);
    if (bar > 0) ps_fill(8, HDR_H + 52, bar, 22, COL_ACCENT);
    char pct[8]; snprintf(pct, sizeof(pct), "%d%%", s_install_pct);
    ps_str(SCR_W / 2 - 12, HDR_H + 56, pct, COL_TEXT,
           s_install_pct > 30 ? COL_ACCENT : COL_DIV, 1);
    ps_str(8, HDR_H + 84, s_install_msg, COL_SUB, COL_BG, 1);
}

// ── SOS crash-loop recovery screen ───────────────────────────────────────────
static void render_sos() {
    s_nbtn = 0;
    ps_fill(0, 0, SCR_W, SCR_H, COL_BG);

    // Red alert bar at top
    ps_fill(0, 0, SCR_W, HDR_H, COL_DANGER);
    ps_str(8, 7, "SOS — CRASH LOOP DETECTED", COL_BG, COL_DANGER, 1);

    // Crash count
    char msg[64];
    snprintf(msg, sizeof(msg), "ota_0 failed to boot %u time%s in a row.",
             s_boot_tries, s_boot_tries == 1 ? "" : "s");
    ps_str(8, HDR_H + 12, msg, COL_WARN, COL_BG, 1);
    ps_str(8, HDR_H + 26, "Recommended: wipe ota_0 and reinstall.", COL_SUB, COL_BG, 1);

    ps_hline(0, HDR_H + 40, SCR_W, COL_DIV);

    // Options
    ps_str(8, HDR_H + 52, "What would you like to do?", COL_TEXT, COL_BG, 1);

    // WIPE OTA 0 — most prominent
    ps_btn(8,   HDR_H + 70, 140, 32, "WIPE OTA 0",   COL_BG,   COL_DANGER, TAG_WIPE(0));
    ps_str(156, HDR_H + 70 + 8, "Erase & reinstall", COL_SUB, COL_BG, 1);

    // BOOT ANYWAY — lets user try again despite the counter
    ps_btn(8,   HDR_H + 112, 140, 32, "BOOT ANYWAY", COL_TEXT, COL_BTN,    TAG_BOOT(0));
    ps_str(156, HDR_H + 112 + 8, "Try ota_0 once more", COL_SUB, COL_BG, 1);

    // DISMISS — go to normal bootloader home without auto-booting
    ps_btn(8,   HDR_H + 154, 140, 32, "DISMISS",     COL_TEXT, COL_BTN,    TAG_NO);
    ps_str(156, HDR_H + 154 + 8, "Open bootloader UI", COL_SUB, COL_BG, 1);

    ps_hline(0, SCR_H - FTR_H - 1, SCR_W, COL_DIV);
    ps_fill(0, SCR_H - FTR_H, SCR_W, FTR_H, COL_BG);
    ps_str(8, SCR_H - FTR_H + 9, "Hold GPIO 0 at boot to always reach this screen.",
           COL_SUB, COL_BG, 1);
}

// ── Background tasks ──────────────────────────────────────────────────────────
static void install_task_fn(void*) {
    bool ok = pm_install((uint8_t)s_inst.slot, s_inst.path, s_inst.name,
               [](int pct, const char* status) {
                   s_install_pct = pct;
                   strlcpy(s_install_msg, status, sizeof(s_install_msg));
                   s_dirty = true;
               });
    // After any install, go to post-install screen so user can restore PURR
    // or boot the new firmware. Even if install failed, let user decide.
    if (ok) {
        s_install_pct = 0;
        s_screen = PB_POST_INSTALL;
    } else {
        strlcpy(s_install_msg, "Install failed.", sizeof(s_install_msg));
        s_screen = PB_HOME;
    }
    s_dirty = true;
    vTaskDelete(nullptr);
}

static void backup_task_fn(void*) {
    bool ok = pm_dump_to_sd((uint8_t)s_install_slot, s_backup_path,
               [](int pct, const char* status) {
                   s_install_pct = pct;
                   strlcpy(s_install_msg, status, sizeof(s_install_msg));
                   s_dirty = true;
               });
    if (!ok) s_backup_path[0] = '\0';  // clear path so post-install hides restore option
    s_install_pct = 0;
    strlcpy(s_install_msg, "", sizeof(s_install_msg));
    s_screen = PB_INSTALL_SELECT;
    s_dirty  = true;
    vTaskDelete(nullptr);
}

static void restore_task_fn(void*) {
    bool ok = pm_install((uint8_t)s_inst.slot, s_backup_path, "PURR OS (restored)",
               [](int pct, const char* status) {
                   s_install_pct = pct;
                   strlcpy(s_install_msg, status, sizeof(s_install_msg));
                   s_dirty = true;
               });
    if (ok) {
        pm_launch((uint8_t)s_inst.slot);  // never returns
    } else {
        strlcpy(s_install_msg, "Restore failed. Check SD card.", sizeof(s_install_msg));
        s_screen = PB_HOME;
        s_dirty  = true;
    }
    vTaskDelete(nullptr);
}

// ── Action handler ────────────────────────────────────────────────────────────
static void on_tap(int tag) {
    int action = tag & 0xF0;
    int slot   = tag & 0x0F;

    if (action == 0x10) {
        // BOOT <slot>
        s_confirm_slot = slot;
        s_screen = PB_CONFIRM_BOOT;
        s_dirty = true;
        return;
    }
    if (action == 0x20) {
        // WIPE <slot>
        s_confirm_slot = slot;
        s_screen = PB_CONFIRM_WIPE;
        s_dirty = true;
        return;
    }
    if (action == 0x30) {
        // INSTALL <slot>
        s_install_slot = slot;
        s_backup_path[0] = '\0';  // clear any previous backup path

        // If the slot has PURR firmware, offer to back it up first
        esp_app_desc_t desc;
        const esp_partition_t* p = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP,
            (esp_partition_subtype_t)(ESP_PARTITION_SUBTYPE_APP_OTA_0 + slot), NULL);
        bool slot_is_purr = (p && esp_ota_get_partition_description(p, &desc) == ESP_OK
                             && strcmp(desc.project_name, "purr_os_core") == 0);

        if (slot_is_purr && pm_sd_available()) {
            s_screen = PB_CONFIRM_BACKUP;
        } else {
            s_nfiles = pm_sd_available() ? pm_sd_list(s_files, PM_SD_MAX_FILES) : 0;
            s_screen = PB_INSTALL_SELECT;
        }
        s_dirty = true;
        return;
    }

    switch (s_screen) {

    case PB_CONFIRM_BACKUP:
        if (tag == TAG_YES) {
            // Start backup then proceed to file picker
            snprintf(s_backup_path, sizeof(s_backup_path),
                     "/PURR_BACKUP_OTA%d.bin", s_install_slot);
            s_install_pct = 0;
            strlcpy(s_install_msg, "Preparing...", sizeof(s_install_msg));
            s_screen = PB_BACKING_UP;
            s_dirty  = true;
            xTaskCreate(backup_task_fn, "pm_backup", 8192, nullptr, 4, nullptr);
        } else if (tag == TAG_NO) {
            s_nfiles = pm_sd_available() ? pm_sd_list(s_files, PM_SD_MAX_FILES) : 0;
            s_screen = PB_INSTALL_SELECT;
        }
        break;

    case PB_POST_INSTALL:
        if (tag == TAG_YES && s_backup_path[0]) {
            // Restore PURR from backup
            s_inst.slot = s_install_slot;
            s_install_pct = 0;
            strlcpy(s_install_msg, "Preparing...", sizeof(s_install_msg));
            s_screen = PB_RESTORING;
            s_dirty  = true;
            xTaskCreate(restore_task_fn, "pm_restore", 8192, nullptr, 4, nullptr);
        } else if (tag == TAG_NO) {
            pm_launch((uint8_t)s_install_slot);  // never returns
        }
        break;

    case PB_SOS:
        if (tag == TAG_NO) {
            // Dismiss — clear counter so normal bootloader home shows
            { nvs_handle_t h; if (nvs_open("purr_bl", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "boot_tries", 0); nvs_commit(h); nvs_close(h); } }
            s_boot_tries = 0;
            s_screen = PB_HOME;
        } else if ((tag & 0xF0) == 0x10) {
            // BOOT ANYWAY — clear counter and chainload
            { nvs_handle_t h; if (nvs_open("purr_bl", NVS_READWRITE, &h) == ESP_OK) { nvs_set_u8(h, "boot_tries", 0); nvs_commit(h); nvs_close(h); } }
            pm_launch(0);  // never returns
        } else if ((tag & 0xF0) == 0x20) {
            // WIPE — go through normal confirm flow
            s_confirm_slot = tag & 0x0F;
            s_screen = PB_CONFIRM_WIPE;
        }
        break;

    case PB_CONFIRM_WIPE:
        if (tag == TAG_YES) {
            pm_delete((uint8_t)s_confirm_slot);
            s_screen = PB_HOME;
        } else if (tag == TAG_NO) {
            s_screen = PB_HOME;
        }
        break;

    case PB_CONFIRM_BOOT:
        if (tag == TAG_YES) {
            pm_launch((uint8_t)s_confirm_slot);  // never returns on success
        } else if (tag == TAG_NO) {
            s_screen = PB_HOME;
        }
        break;

    case PB_INSTALL_SELECT:
        if (tag == TAG_NO) {
            s_screen = PB_HOME;
        } else if ((tag & 0xF0) == 0xF0) {
            int fi = tag & 0x0F;
            if (fi < s_nfiles) {
                s_inst.slot = s_install_slot;
                strlcpy(s_inst.path, s_files[fi].path, PM_PATH_LEN);
                strlcpy(s_inst.name, s_files[fi].name, PM_NAME_LEN);
                s_install_pct = 0;
                strlcpy(s_install_msg, "Preparing...", sizeof(s_install_msg));
                s_screen = PB_INSTALLING;
                s_dirty  = true;
                xTaskCreate(install_task_fn, "pm_inst", 8192, nullptr, 4, nullptr);
                return;
            }
        }
        break;

    default: break;
    }
    s_dirty = true;
}

// ── Touch ─────────────────────────────────────────────────────────────────────
static void handle_touch() {
    cst_touch_event_t ev;
    bool pressed = touch_cst816s_get_event(&ev) && ev.pressed;
    if (pressed && !s_prev_pressed) {
        uint32_t now = millis();
        if (now - s_debounce_ms >= 300) {
            s_debounce_ms = now;
            int tag = hit_test(ev.x, ev.y);
            if (tag >= 0) on_tap(tag);
        }
    }
    s_prev_pressed = pressed;
}

// ── LED (blue in bootloader mode) ────────────────────────────────────────────
#define CYD_LED_R  4
#define CYD_LED_G 16
#define CYD_LED_B 17

static void led_init() {
    pinMode(CYD_LED_R, OUTPUT); digitalWrite(CYD_LED_R, HIGH);
    pinMode(CYD_LED_G, OUTPUT); digitalWrite(CYD_LED_G, HIGH);
    pinMode(CYD_LED_B, OUTPUT); digitalWrite(CYD_LED_B, HIGH);
}
static void led_heartbeat() {
    static uint32_t ms = 0; static bool on = false;
    if (millis() - ms >= 500) { ms = millis(); on = !on;
        digitalWrite(CYD_LED_B, on ? LOW : HIGH); }
}

// ── Main task ─────────────────────────────────────────────────────────────────
static void purr_bootloader_task(void*) {
    led_init();
    pm_init();
    s_dirty = true;

    static uint32_t s_last_tick = 0;

    while (true) {
        handle_touch();

        if (s_dirty) {
            s_dirty = false;
            switch (s_screen) {
            case PB_HOME:             render_home(); break;
            case PB_CONFIRM_WIPE:     render_home(); render_confirm_wipe(); break;
            case PB_CONFIRM_BOOT:     render_home(); render_confirm_boot(); break;
            case PB_CONFIRM_BACKUP:   render_home(); render_confirm_backup(); break;
            case PB_BACKING_UP:       render_backing_up(); break;
            case PB_INSTALL_SELECT:   render_install_select(); break;
            case PB_INSTALLING:       render_installing(); break;
            case PB_POST_INSTALL:     render_post_install(); break;
            case PB_RESTORING:        render_restoring(); break;
            case PB_SOS:              render_sos(); break;
            }
        }

        uint32_t now = millis();
        if (now - s_last_tick >= 2000) {
            s_last_tick = now;
            if (s_screen == PB_HOME) refresh_header_ram();
            else if (s_screen == PB_INSTALLING) s_dirty = true;
        }

        led_heartbeat();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void purr_bootloader_start(bool sos, uint8_t boot_tries) {
    s_screen     = sos ? PB_SOS : PB_HOME;
    s_boot_tries = boot_tries;
    xTaskCreatePinnedToCore(purr_bootloader_task, "purr_boot", 8192, nullptr, 3, nullptr, 1);
}

#endif  // PURR_HAS_BOOTLOADER
