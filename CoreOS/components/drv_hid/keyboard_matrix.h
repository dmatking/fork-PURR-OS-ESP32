#pragma once
#include <stdint.h>
#include <stdbool.h>

// 6×14 matrix from board.md (Page 3)
// Rows driven LOW one at a time; columns read with INPUT_PULLUP
#define MATRIX_ROWS 6
#define MATRIX_COLS 14

// Row GPIO pins — ROW0 first
static const uint8_t ROW_PINS[MATRIX_ROWS] = {
    6,   // ROW0
    14,  // ROW1
    15,  // ROW2
    16,  // ROW3
    17,  // ROW4
    18   // ROW5
};

// Column GPIO pins — COL0 first
static const uint8_t COL_PINS[MATRIX_COLS] = {
    21,  // COL0
    13,  // COL1
    12,  // COL2
    11,  // COL3
    10,  // COL4
    9,   // COL5
    8,   // COL6
    7,   // COL7
    0,   // COL8
    1,   // COL9
    2,   // COL10
    3,   // COL11
    4,   // COL12
    5    // COL13
};

// Key state — raw scan results, debounced
typedef struct {
    bool current[MATRIX_ROWS][MATRIX_COLS];
    bool previous[MATRIX_ROWS][MATRIX_COLS];
    bool debounced[MATRIX_ROWS][MATRIX_COLS];
    uint8_t debounce_count[MATRIX_ROWS][MATRIX_COLS];
} matrix_state_t;

void matrix_init(matrix_state_t* m);
void matrix_scan(matrix_state_t* m);
bool matrix_key_pressed(matrix_state_t* m, uint8_t row, uint8_t col);
bool matrix_key_just_pressed(matrix_state_t* m, uint8_t row, uint8_t col);
bool matrix_key_just_released(matrix_state_t* m, uint8_t row, uint8_t col);
