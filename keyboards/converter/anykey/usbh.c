/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * AnyKey USB host role: Pico-PIO-USB + TinyUSB (host-only), running entirely
 * on RP2040 core 1. Core 0 continues running ChibiOS/QMK exactly as any
 * other vial-qmk RP2040 keyboard; core 1 is dedicated to polling whatever
 * keyboard is plugged into AnyKey's host port and publishing its HID
 * reports into a shared struct that matrix.c reads. See
 * docs/research-findings.md for why this split exists.
 *
 * pico_multicore isn't available in this ChibiOS-based QMK build (it's
 * vendored only as far as the individual hardware_* modules QMK's own
 * RP2040 platform already uses). The two primitives actually needed here -
 * resetting core 1 and handing it an entry point - are reimplemented below,
 * adapted from pico-sdk's src/rp2_common/pico_multicore/multicore.c.
 */

#include <stdint.h>
#include <string.h>

#include "hardware/regs/addressmap.h"
#include "hardware/regs/psm.h"
#include "hardware/regs/sio.h"
#include "hardware/structs/scb.h"
#include "hardware/structs/sio.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#include "pio_usb.h"
#include "tusb.h"

#include "report.h"
#include "usbh.h"

volatile report_keyboard_t anykey_host_report;
volatile bool              anykey_host_report_changed = false;

/* ---- minimal RP2040 core 1 launch ---- */

static void core1_reset(void) {
    volatile uint32_t *power_off = (volatile uint32_t *)(PSM_BASE + PSM_FRCE_OFF_OFFSET);
    *power_off |= PSM_FRCE_OFF_PROC1_BITS;
    while (!(*power_off & PSM_FRCE_OFF_PROC1_BITS)) {
    }
    *power_off &= ~PSM_FRCE_OFF_PROC1_BITS;
}

static void fifo_drain(void) {
    while (sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS) {
        (void)sio_hw->fifo_rd;
    }
}

#define CORE1_LAUNCH_TIMEOUT_US 1000000

/* Returns false on timeout. A stuck/failed core 1 launch must never be able
 * to block matrix_init() (and therefore QMK's entire main loop, including
 * Vial's raw HID processing) forever - see docs/hardware-notes.md. */
static bool fifo_push_blocking(uint32_t data) {
    uint64_t deadline = time_us_64() + CORE1_LAUNCH_TIMEOUT_US;
    while (!(sio_hw->fifo_st & SIO_FIFO_ST_RDY_BITS)) {
        if (time_us_64() > deadline) return false;
    }
    sio_hw->fifo_wr = data;
    __asm volatile("sev");
    return true;
}

static bool fifo_pop_blocking(uint32_t *out) {
    uint64_t deadline = time_us_64() + CORE1_LAUNCH_TIMEOUT_US;
    while (!(sio_hw->fifo_st & SIO_FIFO_ST_VLD_BITS)) {
        __asm volatile("wfe");
        if (time_us_64() > deadline) return false;
    }
    *out = sio_hw->fifo_rd;
    return true;
}

static bool core1_launch(void (*entry)(void), uint32_t *sp) {
    core1_reset();

    const uint32_t cmd_sequence[] = {0, 0, 1, scb_hw->vtor, (uintptr_t)sp, (uintptr_t)entry};

    /* Per-step timeouts in fifo_push/pop_blocking only catch the FIFO going
     * silent. If core 1 responds but with mismatched values, seq resets to 0
     * and the handshake retries indefinitely - each retry individually
     * "succeeds" its own short timeout, so nothing above ever trips. This
     * overall deadline bounds the whole attempt regardless. */
    uint64_t overall_deadline = time_us_64() + CORE1_LAUNCH_TIMEOUT_US;

    uint32_t seq = 0;
    do {
        if (time_us_64() > overall_deadline) return false;

        uint32_t cmd = cmd_sequence[seq];
        if (!cmd) {
            fifo_drain();
            __asm volatile("sev");
        }
        if (!fifo_push_blocking(cmd)) return false;
        uint32_t response;
        if (!fifo_pop_blocking(&response)) return false;
        seq = (cmd == response) ? seq + 1 : 0;
    } while (seq < (sizeof(cmd_sequence) / sizeof(cmd_sequence[0])));
    return true;
}

#define CORE1_STACK_WORDS 2048
static uint32_t core1_stack[CORE1_STACK_WORDS] __attribute__((aligned(8)));

static void core1_entry(void) {
    /* TEMPORARY bisection: run PIO-USB/TinyUSB init (claims PIO state
     * machines + DMA channels) but skip the ongoing tuh_task() polling loop,
     * to isolate init-time corruption from something needing the task loop
     * to actually run. See docs/hardware-notes.md. */
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp                  = ANYKEY_HOST_DP_PIN;

    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    const tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(1, &host_init);

#if 0
    while (1) {
        tuh_task();
    }
#else
    while (1) {
        __asm volatile("wfe");
    }
#endif
}

/* CFG_TUSB_OS is OPT_OS_NONE (no RTOS) - TinyUSB requires the application to
 * supply this itself in that mode (see tinyusb's src/tusb.c). */
uint32_t tusb_time_millis_api(void) {
    return (uint32_t)(time_us_64() / 1000);
}

/* pico-sdk's hardware_irq module (used by pico_time's alarm pool) expects
 * this as its "no handler installed yet" sentinel/fallback. It's normally
 * defined in pico-sdk's own startup file (pico_standard_link/crt0.S), which
 * we don't use - ChibiOS owns startup/the vector table on this build. The
 * sentinel is only ever compared by pointer or, in the pathological case,
 * invoked for a genuinely unclaimed interrupt; a trap is a safe fallback. */
void __unhandled_user_irq(void) {
    while (1) {
    }
}

volatile bool anykey_host_core1_launched = false;

void anykey_usb_host_init(void) {
    gpio_init(ANYKEY_HOST_PWR_PIN);
    gpio_set_dir(ANYKEY_HOST_PWR_PIN, true);
    gpio_put(ANYKEY_HOST_PWR_PIN, true);

    anykey_host_core1_launched = core1_launch(core1_entry, core1_stack + CORE1_STACK_WORDS);
}

/* ---- TinyUSB host HID callbacks (run on core 1) ---- */

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD) {
        tuh_hid_receive_report(dev_addr, instance);
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    (void)dev_addr;
    (void)instance;
    memset((void *)&anykey_host_report, 0, sizeof(anykey_host_report));
    anykey_host_report_changed = true;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len) {
    /* Boot-protocol keyboard report: byte0 mods, byte1 reserved, bytes2-7 keycodes. */
    if (tuh_hid_interface_protocol(dev_addr, instance) == HID_ITF_PROTOCOL_KEYBOARD && len >= 8) {
        anykey_host_report.mods     = report[0];
        anykey_host_report.reserved = report[1];
        memcpy((void *)anykey_host_report.keys, &report[2], KEYBOARD_REPORT_KEYS);
        anykey_host_report_changed = true;
    }
    tuh_hid_receive_report(dev_addr, instance);
}
