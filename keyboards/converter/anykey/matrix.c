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
#include "print.h"
#include "debug.h"

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
    debug_enable = true; /* TEMPORARY Milestone 1 bring-up - see docs/hardware-notes.md */
    print("anykey: matrix_init start\n");
    anykey_usb_host_init();
    print("anykey: matrix_init done\n");
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

/* TEMPORARY Milestone 1 bring-up diagnostic: dprintf()/xprintf() produced no
 * console output at all (confirmed with two different format strings),
 * while plain print() worked reliably - avoiding xprintf entirely with a
 * tiny hand-rolled decimal formatter instead. See docs/hardware-notes.md. */
static void anykey_print_u8(uint8_t v) {
    char buf[4];
    buf[0] = '0' + (v / 100);
    buf[1] = '0' + ((v / 10) % 10);
    buf[2] = '0' + (v % 10);
    buf[3] = '\0';
    print(buf);
}

static void anykey_log_host_status(void) {
    static uint32_t last_log_ms = 0;
    uint32_t        now         = timer_read32();
    if (now - last_log_ms > 2000) {
        last_log_ms = now;
        print("m");
        anykey_print_u8((uint8_t)anykey_host_mount_count);
        print(" h");
        anykey_print_u8((uint8_t)anykey_host_hid_mount_count);
        print("\n");
    }
}

uint8_t matrix_scan(void) {
    bool changed = false;

    anykey_log_host_status();

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
