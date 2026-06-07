#include "keyboard_matrix.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"
#include <string.h>

#define DEBOUNCE_THRESHOLD 3  // consecutive matching reads to register a state change

void matrix_init(matrix_state_t* m) {
    for (int r = 0; r < MATRIX_ROWS; r++) {
        gpio_set_direction((gpio_num_t)ROW_PINS[r], GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)ROW_PINS[r], 1);
    }
    for (int c = 0; c < MATRIX_COLS; c++) {
        gpio_set_direction((gpio_num_t)COL_PINS[c], GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)COL_PINS[c], GPIO_PULLUP_ONLY);
    }
    memset(m, 0, sizeof(*m));
}

void matrix_scan(matrix_state_t* m) {
    memcpy(m->previous, m->debounced, sizeof(m->debounced));

    for (int r = 0; r < MATRIX_ROWS; r++) {
        // Drive this row LOW, all others HIGH
        gpio_set_level((gpio_num_t)ROW_PINS[r], 0);
        esp_rom_delay_us(5);

        for (int c = 0; c < MATRIX_COLS; c++) {
            bool raw = (gpio_get_level((gpio_num_t)COL_PINS[c]) == 0);  // active LOW with pull-up

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

        gpio_set_level((gpio_num_t)ROW_PINS[r], 1);
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
