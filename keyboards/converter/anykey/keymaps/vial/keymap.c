/* SPDX-License-Identifier: GPL-2.0-or-later */
#include QMK_KEYBOARD_H

/* AnyKey's virtual matrix position CODE(row,col) = (row<<4)|col is, by
 * construction (see matrix.c), the raw incoming HID keycode byte. QMK's
 * basic keycodes (KC_A, KC_1, ...) are themselves numerically identical to
 * the USB HID keyboard usage page (KC_A == 0x04, matching the HID spec) -
 * so the default, unmodified-passthrough keymap is simply the identity
 * sequence 0..255. Vial can remap any position live from there. */

#define R16(n) (n) + 0, (n) + 1, (n) + 2, (n) + 3, (n) + 4, (n) + 5, (n) + 6, (n) + 7, (n) + 8, (n) + 9, (n) + 10, (n) + 11, (n) + 12, (n) + 13, (n) + 14, (n) + 15

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = {
        {R16(0)},
        {R16(16)},
        {R16(32)},
        {R16(48)},
        {R16(64)},
        {R16(80)},
        {R16(96)},
        {R16(112)},
        {R16(128)},
        {R16(144)},
        {R16(160)},
        {R16(176)},
        {R16(192)},
        {R16(208)},
        {R16(224)},
        {R16(240)},
    },
};
