/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Virtual matrix driven by AnyKey's USB host role (usbh.c / core 1).
 *
 * Design reused verbatim from QMK's converter/usb_usb/custom_matrix.cpp
 * (Copyright 2016 Jun Wako, ported by Balz Guenat): a 16x16 = 256 position
 * matrix addressed directly by the raw HID keycode byte, so matrix position
 * IS the HID usage ID split into nibbles. Only the report source differs -
 * usb_usb polls the USB Host Shield library from matrix_scan(); AnyKey reads
 * a report populated on core 1 by TinyUSB's host HID callbacks.
 */

#include <stdint.h>
#include <stdbool.h>

#include "keycode.h"
#include "util.h"
#include "matrix.h"
#include "report.h"
#include "host.h"
#include "keyboard.h"
#include "timer.h"
#include "send_string.h"

#include "usbh.h"

#define ROW_MASK 0xF0
#define COL_MASK 0x0F
#define CODE(row, col) (((row) << 4) | (col))
#define ROW(code) (((code)&ROW_MASK) >> 4)
#define COL(code) ((code)&COL_MASK)
#define ROW_BITS(code) (1 << COL(code))

static report_keyboard_t local_keyboard_report;

uint8_t matrix_rows(void) {
    return MATRIX_ROWS;
}
uint8_t matrix_cols(void) {
    return MATRIX_COLS;
}
bool matrix_has_ghost(void) {
    return false;
}

void matrix_init(void) {
    /* TEMPORARY bisection: host role (core 1 / PIO-USB / TinyUSB) disabled
     * entirely to determine whether it's interfering with core 0, given
     * RP2040 has no memory protection between cores. See
     * docs/hardware-notes.md. */
    anykey_usb_host_init();
    matrix_init_kb();
}

__attribute__((weak)) void matrix_init_kb(void) {
    matrix_init_user();
}
__attribute__((weak)) void matrix_init_user(void) {
}
__attribute__((weak)) void matrix_scan_kb(void) {
    matrix_scan_user();
}
__attribute__((weak)) void matrix_scan_user(void) {
}

/* TEMPORARY Milestone 1 bring-up diagnostic: types a status string every ~5s
 * so core 1's launch outcome is directly observable without a debug console
 * (build_vial.mk forces NO_DEBUG), no precise timing needed on the tester's
 * end. Remove once the host role is confirmed working end to end - see
 * docs/hardware-notes.md. */
static void anykey_announce_core1_status(void) {
    static uint32_t last_announce_ms = 0;
    uint32_t        now              = timer_read32();
    if (now - last_announce_ms > 5000) {
        last_announce_ms = now;
        send_string(anykey_host_core1_launched ? "ANYKEY_CORE1_OK " : "ANYKEY_CORE1_TIMEOUT ");
    }
}

uint8_t matrix_scan(void) {
    bool changed = false;

    anykey_announce_core1_status();

    if (anykey_host_report_changed) {
        anykey_host_report_changed = false;
        local_keyboard_report.mods = anykey_host_report.mods;
        for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
            local_keyboard_report.keys[i] = anykey_host_report.keys[i];
        }
        changed = true;
    }

    matrix_scan_kb();
    return changed;
}

bool matrix_is_on(uint8_t row, uint8_t col) {
    uint8_t code = CODE(row, col);

    if (IS_MODIFIER_KEYCODE(code)) {
        if (local_keyboard_report.mods & ROW_BITS(code)) {
            return true;
        }
    }
    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
        if (local_keyboard_report.keys[i] == code) {
            return true;
        }
    }
    return false;
}

matrix_row_t matrix_get_row(uint8_t row) {
    uint16_t row_bits = 0;

    if (IS_MODIFIER_KEYCODE(CODE(row, 0)) && local_keyboard_report.mods) {
        row_bits |= local_keyboard_report.mods;
    }

    for (uint8_t i = 0; i < KEYBOARD_REPORT_KEYS; i++) {
        if (IS_ANY(local_keyboard_report.keys[i])) {
            if (row == ROW(local_keyboard_report.keys[i])) {
                row_bits |= ROW_BITS(local_keyboard_report.keys[i]);
            }
        }
    }
    return row_bits;
}

void matrix_print(void) {
}

void led_set(uint8_t usb_led) {
    led_update_kb((led_t){.raw = usb_led});
}
