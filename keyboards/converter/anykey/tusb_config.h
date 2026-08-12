/* SPDX-License-Identifier: MIT */
/* Host-only TinyUSB config for AnyKey's USB host role (core 1, PIO-based).
 * Core 0's USB device role (to the computer) is handled by ChibiOS's own
 * native USB driver, as for every other vial-qmk RP2040 keyboard - this
 * config never touches the device side. See docs/research-findings.md. */
#pragma once

#define CFG_TUSB_OS OPT_OS_NONE
#define CFG_TUSB_MCU OPT_MCU_RP2040

#define CFG_TUD_ENABLED 0

#define CFG_TUH_ENABLED 1
#define CFG_TUH_RPI_PIO_USB 1

#define CFG_TUH_ENUMERATION_BUFSIZE 256
#define CFG_TUH_HUB 1
#define CFG_TUH_DEVICE_MAX (CFG_TUH_HUB ? 4 : 1)

#define CFG_TUH_HID 4
#define CFG_TUH_HID_EPIN_BUFSIZE 64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif
#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN __attribute__((aligned(4)))
#endif
