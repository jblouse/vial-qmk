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
#include "hardware/structs/clocks.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"

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

/* Pico-PIO-USB's bit-bang timing (usb_tx.pio/usb_rx.pio) computes its PIO
 * clock divider from clock_get_hz(clk_sys) at runtime, with an explicit
 * precondition in the source comments: "clk_sys should be multiply of
 * 12MHz." ChibiOS's RP2040 port configures the pico-sdk stock default of
 * 125MHz (confirmed: SYS_CLK_KHZ in hardware/platform_defs.h), which is
 * NOT a clean multiple of 12MHz (125/12 = 10.41666...). Retarget clk_sys to
 * 120MHz (VCO 1200MHz / (POSTDIV1=5 * POSTDIV2=2) = 120MHz, FBDIV=100 - a
 * standard, well-documented RP2040 PLL configuration) before launching
 * core 1, following the exact same switch-away/reconfigure/switch-back
 * sequence pico-sdk's own clocks_init() uses (see
 * lib/pico-sdk/src/rp2_common/hardware_clocks/clocks.c). PLL_USB (the
 * native USB device role's 48MHz clock) is a separate PLL untouched by
 * this - confirmed via clocks_init()'s own structure and via keeping the
 * device role working throughout testing.
 *
 * Known tradeoff: ChibiOS's kernel SysTick was calibrated for 125MHz at
 * boot; retargeting clk_sys afterward without recalibrating that reload
 * value means software timing (QMK's millisecond timer, debounce, etc.)
 * runs ~4% slower than real time post-change. Not corrected here - no
 * QMK-level timing in this design is hard-real-time-sensitive enough for
 * that to matter, and leaving it uncorrected avoids reaching into
 * ChibiOS's own kernel state from application code. */
static void anykey_fix_sys_clock(void) {
    hw_clear_bits(&clocks_hw->clk[clk_sys].ctrl, CLOCKS_CLK_SYS_CTRL_SRC_BITS);
    while (clocks_hw->clk[clk_sys].selected != 0x1) {
    }

    pll_init(pll_sys, PLL_COMMON_REFDIV, 1200 * MHZ, 5, 2);

    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX, CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, 120000 * KHZ, 120000 * KHZ);
}

#define CORE1_STACK_WORDS 2048
static uint32_t core1_stack[CORE1_STACK_WORDS] __attribute__((aligned(8)));

static void core1_entry(void) {
    pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
    pio_cfg.pin_dp                  = ANYKEY_HOST_DP_PIN;
    /* PIO_USB_DEFAULT_CONFIG hardcodes DMA channel 0 for its TX path
     * (PIO_USB_DMA_TX_DEFAULT - not dynamically claimed via
     * dma_claim_unused_channel()), which collided with something else in
     * this build also assuming channel 0: with the default, tuh_task()
     * running silently corrupted core 0's state (RP2040 has no memory
     * protection between DMA masters) - core 0 kept running but stopped
     * doing useful work, confirmed via bisection. Channel 8 was tested and
     * confirmed stable; see docs/hardware-notes.md for the full bisection
     * that found this. */
    pio_cfg.tx_ch = 8;

    tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg);

    const tusb_rhport_init_t host_init = {
        .role  = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_FULL,
    };
    tusb_init(1, &host_init);

    while (1) {
        tuh_task();
    }
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
    anykey_fix_sys_clock();

    gpio_init(ANYKEY_HOST_PWR_PIN);
    gpio_set_dir(ANYKEY_HOST_PWR_PIN, true);
    gpio_put(ANYKEY_HOST_PWR_PIN, true);

    anykey_host_core1_launched = core1_launch(core1_entry, core1_stack + CORE1_STACK_WORDS);
}

/* TEMPORARY Milestone 1 bring-up diagnostics - see docs/hardware-notes.md. */
volatile uint32_t anykey_host_mount_count     = 0;
volatile uint32_t anykey_host_hid_mount_count = 0;

/* ---- TinyUSB host callbacks (run on core 1) ---- */

/* Device-level (fires for any USB device, any class, before HID binding). */
void tuh_mount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    anykey_host_mount_count++;
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len) {
    (void)desc_report;
    (void)desc_len;
    anykey_host_hid_mount_count++;
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
