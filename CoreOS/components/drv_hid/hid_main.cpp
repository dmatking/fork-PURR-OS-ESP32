#include "keyboard_matrix.h"
#include "keymap.h"
#include "usb_hid.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "hid";
static matrix_state_t matrix;

static void build_report(hid_keyboard_report_t* report) {
    memset(report, 0, sizeof(*report));
    int key_slot = 0;

    for (int i = 0; i < MODIFIER_COUNT; i++) {
        const modifier_def_t* m = &MODIFIERS[i];
        if (matrix_key_pressed(&matrix, m->row, m->col))
            report->modifiers |= m->bit;
    }

    for (int r = 0; r < MATRIX_ROWS && key_slot < 6; r++) {
        for (int c = 0; c < MATRIX_COLS && key_slot < 6; c++) {
            uint8_t code = KEYMAP[r][c];
            if (code == 0x00) continue;
            if (!matrix_key_pressed(&matrix, r, c)) continue;

            bool is_mod = false;
            for (int i = 0; i < MODIFIER_COUNT; i++) {
                if (MODIFIERS[i].row == r && MODIFIERS[i].col == c) { is_mod = true; break; }
            }
            if (is_mod) continue;
            report->keys[key_slot++] = code;
        }
    }
}

static void hid_task(void*) {
    ESP_LOGI(TAG, "cattohid boot");
    matrix_init(&matrix);
    usb_hid_init();
    ESP_LOGI(TAG, "cattohid ready");

    for (;;) {
        matrix_scan(&matrix);
        hid_keyboard_report_t report;
        build_report(&report);
        usb_hid_send_report(&report);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void hid_start() {
    xTaskCreatePinnedToCore(hid_task, "hid", 4096, nullptr, 5, nullptr, 1);
}
