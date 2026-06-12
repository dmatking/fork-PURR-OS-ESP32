#pragma once
#include <stdint.h>

// HID keycodes from USB HID Usage Tables (HUT 1.4)
// Layout: standard 84-key QWERTY for a 6-row × 14-col matrix.
// Adjust per the actual PCB switch positions once board is in hand.
// 0x00 = no key / unused position

// clang-format off
static const uint8_t KEYMAP[6][14] = {
//  COL0    COL1    COL2    COL3    COL4    COL5    COL6    COL7    COL8    COL9    COL10   COL11   COL12   COL13
  { 0x29,   0x1E,   0x1F,   0x20,   0x21,   0x22,   0x23,   0x24,   0x25,   0x26,   0x27,   0x2D,   0x2E,   0x2A }, // ROW0: ESC  1    2    3    4    5    6    7    8    9    0    -    =    BKSP
  { 0x2B,   0x14,   0x1A,   0x08,   0x15,   0x17,   0x1C,   0x18,   0x0C,   0x12,   0x13,   0x2F,   0x30,   0x31 }, // ROW1: TAB  Q    W    E    R    T    Y    U    I    O    P    [    ]    bsls
  { 0x39,   0x04,   0x16,   0x07,   0x09,   0x0A,   0x0B,   0x0D,   0x0E,   0x0F,   0x33,   0x34,   0x00,   0x28 }, // ROW2: CAPS A    S    D    F    G    H    J    K    L    ;    '    ---  ENT
  { 0xE1,   0x00,   0x1D,   0x1B,   0x06,   0x19,   0x05,   0x11,   0x10,   0x36,   0x37,   0x38,   0x00,   0xE5 }, // ROW3: LSFT ---  Z    X    C    V    B    N    M    ,    .    /    ---  RSFT
  { 0xE0,   0xE3,   0xE2,   0x00,   0x00,   0x2C,   0x00,   0x00,   0x00,   0xE6,   0xE7,   0x65,   0x4F,   0xE4 }, // ROW4: LCTL LGUI LALT ---  ---  SPC  ---  ---  ---  RALT RGUI MENU RARW RCTL
  { 0x00,   0x49,   0x4A,   0x4B,   0x00,   0x4E,   0x00,   0x4B,   0x52,   0x51,   0x50,   0x4F,   0x00,   0x00 }  // ROW5: ---  INS  HOME PGUP ---  PGDN ---  DEL  UP   DOWN LEFT RGHT ---  ---
};
// clang-format on

// Modifier bit flags (HID modifier byte)
#define HID_MOD_LCTRL   0x01
#define HID_MOD_LSHIFT  0x02
#define HID_MOD_LALT    0x04
#define HID_MOD_LGUI    0x08
#define HID_MOD_RCTRL   0x10
#define HID_MOD_RSHIFT  0x20
#define HID_MOD_RALT    0x40
#define HID_MOD_RGUI    0x80

// Which matrix positions are modifier keys (checked separately)
typedef struct { uint8_t row; uint8_t col; uint8_t bit; } modifier_def_t;

static const modifier_def_t MODIFIERS[] = {
    { 3, 0,  HID_MOD_LSHIFT },   // ROW3 COL0  = LSHIFT
    { 3, 13, HID_MOD_RSHIFT },   // ROW3 COL13 = RSHIFT
    { 4, 0,  HID_MOD_LCTRL  },   // ROW4 COL0  = LCTRL
    { 4, 2,  HID_MOD_LALT   },   // ROW4 COL2  = LALT
    { 4, 1,  HID_MOD_LGUI   },   // ROW4 COL1  = LGUI
    { 4, 9,  HID_MOD_RALT   },   // ROW4 COL9  = RALT
    { 4, 10, HID_MOD_RGUI   },   // ROW4 COL10 = RGUI
    { 4, 13, HID_MOD_RCTRL  },   // ROW4 COL13 = RCTRL
};

static const int MODIFIER_COUNT = sizeof(MODIFIERS) / sizeof(MODIFIERS[0]);
