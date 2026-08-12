/* SPDX-License-Identifier: BSD-3-Clause */
/*
 * Minimal stand-in for pico-sdk's pico/stdlib.h umbrella header, which isn't
 * present in vial-qmk's sparse pico-sdk checkout (only the individual
 * hardware_* modules QMK's own RP2040 platform uses are vendored). Vendored
 * Pico-PIO-USB only needs gpio_pull_down() and the busy_wait_*() family from
 * this umbrella in practice - both already available via the headers below,
 * whose implementations (gpio.c, timer.c) QMK's RP2040 platform already
 * compiles into every build (platforms/chibios/vendors/RP/RP2040.mk).
 */
#pragma once

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "pico/time.h"
