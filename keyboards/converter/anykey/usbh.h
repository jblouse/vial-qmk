/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <stdbool.h>
#include "report.h"

/* Populated on core 1 (TinyUSB host callbacks), read on core 0 (matrix.c).
 * Plain volatile shared state - acceptable for a hobby passthrough device;
 * see docs/research-findings.md for the reasoning. */
extern volatile report_keyboard_t anykey_host_report;
extern volatile bool              anykey_host_report_changed;

/* Powers the host port, launches core 1, and starts the PIO-USB + TinyUSB
 * host stack there. Call once from matrix_init(). */
void anykey_usb_host_init(void);
