#include "launcher.h"
#include "../../system/kernel/modules/partition_manager.h"
#include "../../system/kernel/kitt.h"
#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ── Colour palette ────────────────────────────────────────────────────────────
#define C_BG        0x0D1117   // near-black
#define C_CARD      0x161B22   // dark card
#define C_CARD_HOV  0x21262D   // hovered card
#define C_ACCENT    0x58A6FF   // PURR blue
#define C_GREEN     0x3FB950   // occupied slot
#define C_RED       0xF85149   // delete / error
#define C_TEXT      0xC9D1D9   // body text
#define C_MUTED     0x6E7681   // secondary text
#define C_BORDER    0x30363D   // card border

// ── Screen metrics (320×240) ──────────────────────────────────────────────────
#define SB_H    28   // status bar height
#define PAD      8
#define CARD_H  80   // height of each firmware card
#define CARD_W  (320 - PAD * 2)

// ── Forward declarations ──────────────────────────────────────────────────────
extern KITT kitt;

static void build_main_screen();
static void build_install_screen(uint8_t slot);
static void build_progress_screen(const char* title);
static void update_progress(int pct, const char* msg);

// ── Shared LVGL objects (main screen) ────────────────────────────────────────
static lv_obj_t* scr_main     = nullptr;
static lv_obj_t* scr_install  = nullptr;
static lv_obj_t* scr_progress = nullptr;
static lv_obj_t* progress_bar = nullptr;
static lv_obj_t* progress_lbl = nullptr;

static uint8_t install_target_slot = 0;

// ── Status bar ────────────────────────────────────────────────────────────────

static void build_status_bar(lv_obj_t* parent) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_set_size(bar, 320, SB_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    // OS name (left)
    lv_obj_t* os = lv_label_create(bar);
    lv_label_set_text(os, "PURR OS");
    lv_obj_set_style_text_color(os, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_text_font(os, &lv_font_montserrat_14, 0);
    lv_obj_align(os, LV_ALIGN_LEFT_MID, PAD, 0);

    // Battery (right)
    lv_obj_t* batt = lv_label_create(bar);
    char batt_str[16];
    snprintf(batt_str, sizeof(batt_str), "%d%%", kitt.battery_percent());
    lv_label_set_text(batt, batt_str);
    lv_obj_set_style_text_color(batt, lv_color_hex(C_MUTED), 0);
    lv_obj_set_style_text_font(batt, &lv_font_montserrat_12, 0);
    lv_obj_align(batt, LV_ALIGN_RIGHT_MID, -PAD, 0);

    // WiFi indicator
    lv_obj_t* wifi = lv_label_create(bar);
    lv_label_set_text(wifi, kitt.wifi_connected() ? "WiFi" : "");
    lv_obj_set_style_text_color(wifi, lv_color_hex(C_GREEN), 0);
    lv_obj_set_style_text_font(wifi, &lv_font_montserrat_12, 0);
    lv_obj_align(wifi, LV_ALIGN_RIGHT_MID, -52, 0);
}

// ── Firmware card ─────────────────────────────────────────────────────────────

typedef struct {
    uint8_t slot;
} card_ctx_t;

static card_ctx_t card_ctx[PM_MAX_SLOTS];

static void on_launch_clicked(lv_event_t* e) {
    card_ctx_t* ctx = (card_ctx_t*)lv_event_get_user_data(e);
    pm_launch(ctx->slot);  // reboots — no return
}

static void on_delete_clicked(lv_event_t* e) {
    card_ctx_t* ctx = (card_ctx_t*)lv_event_get_user_data(e);
    pm_delete(ctx->slot);
    build_main_screen();
    lv_scr_load(scr_main);
}

static void on_install_clicked(lv_event_t* e) {
    card_ctx_t* ctx = (card_ctx_t*)lv_event_get_user_data(e);
    install_target_slot = ctx->slot;
    build_install_screen(ctx->slot);
    lv_scr_load(scr_install);
}

static void build_firmware_card(lv_obj_t* parent, uint8_t slot, int y_pos) {
    pm_slot_t info;
    bool ok = pm_slot_info(slot, &info);
    if (!ok) return;

    card_ctx[slot].slot = slot;

    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_set_pos(card, PAD, y_pos);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 6, 0);
    lv_obj_set_style_pad_all(card, PAD, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    // Slot label
    lv_obj_t* lbl_slot = lv_label_create(card);
    char slot_str[16];
    snprintf(slot_str, sizeof(slot_str), "Slot %u", slot);
    lv_label_set_text(lbl_slot, slot_str);
    lv_obj_set_style_text_color(lbl_slot, lv_color_hex(C_MUTED), 0);
    lv_obj_set_style_text_font(lbl_slot, &lv_font_montserrat_10, 0);
    lv_obj_set_pos(lbl_slot, 0, 0);

    if (info.valid) {
        // Firmware name
        lv_obj_t* lbl_name = lv_label_create(card);
        lv_label_set_text(lbl_name, info.name);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
        lv_obj_set_pos(lbl_name, 0, 14);
        lv_obj_set_width(lbl_name, CARD_W - 16 - 70);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);

        // Size
        lv_obj_t* lbl_size = lv_label_create(card);
        char size_str[24];
        snprintf(size_str, sizeof(size_str), "%u KB", (unsigned)(info.size_bytes / 1024));
        lv_label_set_text(lbl_size, size_str);
        lv_obj_set_style_text_color(lbl_size, lv_color_hex(C_MUTED), 0);
        lv_obj_set_style_text_font(lbl_size, &lv_font_montserrat_10, 0);
        lv_obj_set_pos(lbl_size, 0, 32);

        // LAUNCH button
        lv_obj_t* btn_launch = lv_btn_create(card);
        lv_obj_set_size(btn_launch, 70, 28);
        lv_obj_align(btn_launch, LV_ALIGN_RIGHT_MID, 0, -10);
        lv_obj_set_style_bg_color(btn_launch, lv_color_hex(C_ACCENT), 0);
        lv_obj_set_style_radius(btn_launch, 4, 0);
        lv_obj_t* bl = lv_label_create(btn_launch);
        lv_label_set_text(bl, "LAUNCH");
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
        lv_obj_center(bl);
        lv_obj_add_event_cb(btn_launch, on_launch_clicked, LV_EVENT_CLICKED, &card_ctx[slot]);

        // DELETE button
        lv_obj_t* btn_del = lv_btn_create(card);
        lv_obj_set_size(btn_del, 70, 28);
        lv_obj_align(btn_del, LV_ALIGN_RIGHT_MID, 0, 22);
        lv_obj_set_style_bg_color(btn_del, lv_color_hex(C_RED), 0);
        lv_obj_set_style_radius(btn_del, 4, 0);
        lv_obj_t* dl = lv_label_create(btn_del);
        lv_label_set_text(dl, "DELETE");
        lv_obj_set_style_text_font(dl, &lv_font_montserrat_10, 0);
        lv_obj_center(dl);
        lv_obj_add_event_cb(btn_del, on_delete_clicked, LV_EVENT_CLICKED, &card_ctx[slot]);

    } else {
        // Empty slot
        lv_obj_t* lbl_empty = lv_label_create(card);
        lv_label_set_text(lbl_empty, "empty");
        lv_obj_set_style_text_color(lbl_empty, lv_color_hex(C_MUTED), 0);
        lv_obj_set_style_text_font(lbl_empty, &lv_font_montserrat_12, 0);
        lv_obj_set_pos(lbl_empty, 0, 18);

        // INSTALL button
        lv_obj_t* btn_inst = lv_btn_create(card);
        lv_obj_set_size(btn_inst, 80, 32);
        lv_obj_align(btn_inst, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn_inst, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_radius(btn_inst, 4, 0);
        lv_obj_t* il = lv_label_create(btn_inst);
        lv_label_set_text(il, "+ INSTALL");
        lv_obj_set_style_text_font(il, &lv_font_montserrat_10, 0);
        lv_obj_center(il);
        lv_obj_add_event_cb(btn_inst, on_install_clicked, LV_EVENT_CLICKED, &card_ctx[slot]);
    }
}

// ── Main screen ───────────────────────────────────────────────────────────────

static void build_main_screen() {
    if (scr_main) lv_obj_del(scr_main);
    scr_main = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_main, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(scr_main, 0, 0);
    lv_obj_set_style_radius(scr_main, 0, 0);
    lv_obj_clear_flag(scr_main, LV_OBJ_FLAG_SCROLLABLE);

    build_status_bar(scr_main);

    uint8_t slots = pm_slot_count();
    for (uint8_t i = 0; i < slots; i++) {
        int y = SB_H + PAD + i * (CARD_H + PAD);
        build_firmware_card(scr_main, i, y);
    }

    if (slots == 0) {
        lv_obj_t* lbl = lv_label_create(scr_main);
        lv_label_set_text(lbl, "No OTA partitions found.\nCheck partition table.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    }

    // Footer
    lv_obj_t* footer = lv_label_create(scr_main);
    lv_label_set_text_fmt(footer, "PURR OS  |  %s", kitt.device_name());
    lv_obj_set_style_text_color(footer, lv_color_hex(C_MUTED), 0);
    lv_obj_set_style_text_font(footer, &lv_font_montserrat_10, 0);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// ── Install screen (file picker from SD) ─────────────────────────────────────

typedef struct {
    uint8_t  slot;
    uint8_t  file_idx;
} install_ctx_t;

static install_ctx_t install_ctxs[PM_SD_MAX_FILES];
static pm_sd_file_t  sd_files[PM_SD_MAX_FILES];
static int           sd_file_count = 0;

static void on_file_install(lv_event_t* e) {
    install_ctx_t* ctx = (install_ctx_t*)lv_event_get_user_data(e);
    pm_sd_file_t* f = &sd_files[ctx->file_idx];

    build_progress_screen(f->name);
    lv_scr_load(scr_progress);
    lv_timer_handler();

    bool ok = pm_install(ctx->slot, f->path, f->name, update_progress);

    if (ok) {
        build_main_screen();
        lv_scr_load(scr_main);
    } else {
        lv_label_set_text(progress_lbl, "Install failed!");
        delay(2000);
        build_main_screen();
        lv_scr_load(scr_main);
    }
}

static void on_install_back(lv_event_t* e) {
    (void)e;
    lv_scr_load(scr_main);
}

static void build_install_screen(uint8_t slot) {
    if (scr_install) lv_obj_del(scr_install);
    scr_install = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_install, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(scr_install, 0, 0);
    lv_obj_set_style_radius(scr_install, 0, 0);
    lv_obj_clear_flag(scr_install, LV_OBJ_FLAG_SCROLLABLE);

    // Header
    lv_obj_t* hdr = lv_label_create(scr_install);
    lv_label_set_text_fmt(hdr, "Install → Slot %u", slot);
    lv_obj_set_style_text_color(hdr, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_text_font(hdr, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(hdr, PAD, 6);

    // Back button
    lv_obj_t* btn_back = lv_btn_create(scr_install);
    lv_obj_set_size(btn_back, 50, 24);
    lv_obj_align(btn_back, LV_ALIGN_TOP_RIGHT, -PAD, 4);
    lv_obj_set_style_bg_color(btn_back, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(btn_back, 1, 0);
    lv_obj_set_style_radius(btn_back, 4, 0);
    lv_obj_t* bl = lv_label_create(btn_back);
    lv_label_set_text(bl, "Back");
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_10, 0);
    lv_obj_center(bl);
    lv_obj_add_event_cb(btn_back, on_install_back, LV_EVENT_CLICKED, NULL);

    if (!pm_sd_available()) {
        lv_obj_t* lbl = lv_label_create(scr_install);
        lv_label_set_text(lbl, "No SD card detected.\nInsert SD and restart.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        return;
    }

    sd_file_count = pm_sd_list(sd_files, PM_SD_MAX_FILES);

    if (sd_file_count == 0) {
        lv_obj_t* lbl = lv_label_create(scr_install);
        lv_label_set_text(lbl, "No .bin files on SD card.");
        lv_obj_set_style_text_color(lbl, lv_color_hex(C_MUTED), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    // Scrollable file list
    lv_obj_t* list = lv_obj_create(scr_install);
    lv_obj_set_size(list, 320, 204);
    lv_obj_set_pos(list, 0, 36);
    lv_obj_set_style_bg_color(list, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_all(list, PAD, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    for (int i = 0; i < sd_file_count; i++) {
        install_ctxs[i].slot     = slot;
        install_ctxs[i].file_idx = (uint8_t)i;

        lv_obj_t* row = lv_obj_create(list);
        lv_obj_set_size(row, 304, 44);
        lv_obj_set_style_bg_color(row, lv_color_hex(C_CARD), 0);
        lv_obj_set_style_border_color(row, lv_color_hex(C_BORDER), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* lbl_name = lv_label_create(row);
        lv_label_set_text(lbl_name, sd_files[i].name);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(C_TEXT), 0);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_12, 0);
        lv_obj_set_width(lbl_name, 200);
        lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_DOT);
        lv_obj_align(lbl_name, LV_ALIGN_LEFT_MID, 0, -8);

        lv_obj_t* lbl_size = lv_label_create(row);
        char sz[16];
        snprintf(sz, sizeof(sz), "%u KB", (unsigned)(sd_files[i].size_bytes / 1024));
        lv_label_set_text(lbl_size, sz);
        lv_obj_set_style_text_color(lbl_size, lv_color_hex(C_MUTED), 0);
        lv_obj_set_style_text_font(lbl_size, &lv_font_montserrat_10, 0);
        lv_obj_align(lbl_size, LV_ALIGN_LEFT_MID, 0, 8);

        lv_obj_t* btn = lv_btn_create(row);
        lv_obj_set_size(btn, 60, 28);
        lv_obj_align(btn, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(C_GREEN), 0);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_t* bl2 = lv_label_create(btn);
        lv_label_set_text(bl2, "Flash");
        lv_obj_set_style_text_font(bl2, &lv_font_montserrat_10, 0);
        lv_obj_center(bl2);
        lv_obj_add_event_cb(btn, on_file_install, LV_EVENT_CLICKED, &install_ctxs[i]);
    }
}

// ── Progress screen ───────────────────────────────────────────────────────────

static void build_progress_screen(const char* title) {
    if (scr_progress) lv_obj_del(scr_progress);
    scr_progress = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr_progress, lv_color_hex(C_BG), 0);
    lv_obj_set_style_border_width(scr_progress, 0, 0);
    lv_obj_set_style_radius(scr_progress, 0, 0);
    lv_obj_clear_flag(scr_progress, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl_title = lv_label_create(scr_progress);
    lv_label_set_text_fmt(lbl_title, "Installing: %s", title);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(C_TEXT), 0);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_title, LV_ALIGN_CENTER, 0, -30);
    lv_obj_set_width(lbl_title, 280);
    lv_label_set_long_mode(lbl_title, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(lbl_title, LV_TEXT_ALIGN_CENTER, 0);

    progress_bar = lv_bar_create(scr_progress);
    lv_obj_set_size(progress_bar, 260, 16);
    lv_obj_align(progress_bar, LV_ALIGN_CENTER, 0, 10);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(progress_bar, lv_color_hex(C_CARD), LV_PART_MAIN);

    progress_lbl = lv_label_create(scr_progress);
    lv_label_set_text(progress_lbl, "Starting...");
    lv_obj_set_style_text_color(progress_lbl, lv_color_hex(C_MUTED), 0);
    lv_obj_set_style_text_font(progress_lbl, &lv_font_montserrat_10, 0);
    lv_obj_align(progress_lbl, LV_ALIGN_CENTER, 0, 36);
}

static void update_progress(int pct, const char* msg) {
    if (progress_bar) lv_bar_set_value(progress_bar, pct, LV_ANIM_OFF);
    if (progress_lbl) lv_label_set_text(progress_lbl, msg);
    lv_timer_handler();
}

// ── FreeRTOS task ─────────────────────────────────────────────────────────────

static void launcher_task(void* arg) {
    (void)arg;
    pm_init();
    build_main_screen();
    lv_scr_load(scr_main);

    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void launcher_start() {
    xTaskCreatePinnedToCore(launcher_task, "launcher", 8192, nullptr, 2, nullptr, 1);
}
