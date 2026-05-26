#include <Arduino.h>
#include "keyboard_matrix.h"
#include "keymap.h"
#include "usb_hid.h"

static matrix_state_t matrix;

// Build the HID report from current debounced matrix state
static void build_report(hid_keyboard_report_t* report) {
    memset(report, 0, sizeof(*report));
    int key_slot = 0;

    // Collect modifiers
    for (int i = 0; i < MODIFIER_COUNT; i++) {
        const modifier_def_t* m = &MODIFIERS[i];
        if (matrix_key_pressed(&matrix, m->row, m->col))
            report->modifiers |= m->bit;
    }

    // Collect regular keys (up to 6KRO)
    for (int r = 0; r < MATRIX_ROWS && key_slot < 6; r++) {
        for (int c = 0; c < MATRIX_COLS && key_slot < 6; c++) {
            uint8_t code = KEYMAP[r][c];
            if (code == 0x00) continue;                  // unused position
            if (!matrix_key_pressed(&matrix, r, c)) continue;

            // Skip if this position is a modifier (already handled)
            bool is_mod = false;
            for (int i = 0; i < MODIFIER_COUNT; i++) {
                if (MODIFIERS[i].row == r && MODIFIERS[i].col == c) {
                    is_mod = true;
                    break;
                }
            }
            if (is_mod) continue;

            report->keys[key_slot++] = code;
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("[cattohid] boot");

    matrix_init(&matrix);
    usb_hid_init();

    Serial.println("[cattohid] ready");
}

void loop() {
    matrix_scan(&matrix);

    hid_keyboard_report_t report;
    build_report(&report);
    usb_hid_send_report(&report);

    delay(1);  // ~1ms scan cycle — fast enough for responsive typing
}
