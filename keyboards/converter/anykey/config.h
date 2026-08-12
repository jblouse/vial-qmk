/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

/* Virtual matrix: one row/col slot per possible HID keycode byte (0x00-0xFF).
 * Matrix position IS the HID usage ID, split into nibbles - see matrix.c.
 * Pattern reused verbatim from QMK's converter/usb_usb/custom_matrix.cpp. */
#define MATRIX_ROWS 16
#define MATRIX_COLS 16

/* Adafruit Feather RP2040 with USB Type-A Host (product #5723).
 * See docs/hardware-notes.md in the repo root for the full pin table. */
#define ANYKEY_HOST_DP_PIN 16  /* host D+; D- is fixed at pin_dp + 1 by Pico-PIO-USB */
#define ANYKEY_HOST_PWR_PIN 18 /* host port 5V enable */
