#include "keyboard_matrix.h"
#include <Arduino.h>

#define DEBOUNCE_THRESHOLD 3  // consecutive matching reads to register a state change

void matrix_init(matrix_state_t* m) {
    for (int r = 0; r < MATRIX_ROWS; r++) {
        pinMode(ROW_PINS[r], OUTPUT);
        digitalWrite(ROW_PINS[r], HIGH);  // idle high
    }
    for (int c = 0; c < MATRIX_COLS; c++) {
        pinMode(COL_PINS[c], INPUT_PULLUP);
    }
    memset(m, 0, sizeof(*m));
}

void matrix_scan(matrix_state_t* m) {
    memcpy(m->previous, m->debounced, sizeof(m->debounced));

    for (int r = 0; r < MATRIX_ROWS; r++) {
        // Drive this row LOW, all others HIGH
        digitalWrite(ROW_PINS[r], LOW);
        delayMicroseconds(5);  // settle time for capacitance on traces

        for (int c = 0; c < MATRIX_COLS; c++) {
            bool raw = (digitalRead(COL_PINS[c]) == LOW);  // active LOW with pull-up

            if (raw == m->current[r][c]) {
                if (m->debounce_count[r][c] < DEBOUNCE_THRESHOLD)
                    m->debounce_count[r][c]++;
                if (m->debounce_count[r][c] >= DEBOUNCE_THRESHOLD)
                    m->debounced[r][c] = raw;
            } else {
                m->current[r][c]      = raw;
                m->debounce_count[r][c] = 0;
            }
        }

        digitalWrite(ROW_PINS[r], HIGH);
    }
}

bool matrix_key_pressed(matrix_state_t* m, uint8_t row, uint8_t col) {
    return m->debounced[row][col];
}

bool matrix_key_just_pressed(matrix_state_t* m, uint8_t row, uint8_t col) {
    return m->debounced[row][col] && !m->previous[row][col];
}

bool matrix_key_just_released(matrix_state_t* m, uint8_t row, uint8_t col) {
    return !m->debounced[row][col] && m->previous[row][col];
}
