# AnyKey: USB-host-to-USB-device keyboard converter on Adafruit Feather RP2040 USB Host.
# See docs/research-findings.md (repo root) for why this is structured the way it is.

CUSTOM_MATRIX = yes
SRC += matrix.c usbh.c

ANYKEY_DIR      := $(KEYBOARD_PATH_1)
TINYUSB_DIR     := $(ANYKEY_DIR)/lib/tinyusb
PICO_PIO_USB_DIR := $(ANYKEY_DIR)/lib/pico-pio-usb

# pico-sdk modules that are vendored in this checkout but not wired into
# QMK's default RP2040 platform sources (platforms/chibios/vendors/RP/RP2040.mk),
# because QMK's own build never needed them - pico_pio_usb's host driver does:
# hardware_dma (DMA-driven bit encoding), and the pico_time -> pico_sync ->
# pico_util/pheap chain (its repeating "start of frame" timer).
PICOSDKROOT := $(TOP_DIR)/lib/pico-sdk

SRC += $(PICOSDKROOT)/src/rp2_common/hardware_dma/dma.c
EXTRAINCDIRS += $(PICOSDKROOT)/src/rp2_common/hardware_dma/include

SRC += $(PICOSDKROOT)/src/common/pico_time/time.c
SRC += $(PICOSDKROOT)/src/common/pico_sync/critical_section.c
SRC += $(PICOSDKROOT)/src/common/pico_sync/lock_core.c
SRC += $(PICOSDKROOT)/src/common/pico_sync/mutex.c
SRC += $(PICOSDKROOT)/src/common/pico_util/pheap.c
SRC += $(PICOSDKROOT)/src/rp2_common/hardware_sync/sync.c
SRC += $(PICOSDKROOT)/src/rp2_common/hardware_irq/irq.c
SRC += $(PICOSDKROOT)/src/rp2_common/hardware_irq/irq_handler_chain.S
EXTRAINCDIRS += $(PICOSDKROOT)/src/common/pico_time/include
EXTRAINCDIRS += $(PICOSDKROOT)/src/common/pico_sync/include
EXTRAINCDIRS += $(PICOSDKROOT)/src/common/pico_util/include

# TinyUSB, host-only (device role stays on ChibiOS's own native USB driver).
SRC += $(TINYUSB_DIR)/src/tusb.c
SRC += $(TINYUSB_DIR)/src/common/tusb_fifo.c
SRC += $(TINYUSB_DIR)/src/host/usbh.c
SRC += $(TINYUSB_DIR)/src/host/hub.c
SRC += $(TINYUSB_DIR)/src/class/hid/hid_host.c
SRC += $(TINYUSB_DIR)/src/portable/raspberrypi/pio_usb/hcd_pio_usb.c
EXTRAINCDIRS += $(TINYUSB_DIR)/src

# Pico-PIO-USB (the PIO-based USB host peripheral TinyUSB's host stack rides on).
SRC += $(PICO_PIO_USB_DIR)/src/pio_usb.c
SRC += $(PICO_PIO_USB_DIR)/src/pio_usb_host.c
SRC += $(PICO_PIO_USB_DIR)/src/pio_usb_device.c
SRC += $(PICO_PIO_USB_DIR)/src/usb_crc.c
EXTRAINCDIRS += $(PICO_PIO_USB_DIR)/src

EXTRAINCDIRS += $(ANYKEY_DIR)

# Pico-PIO-USB includes pico/stdlib.h, which isn't present in vial-qmk's
# sparse pico-sdk checkout - see pico_shim/pico/stdlib.h for the minimal
# stand-in.
EXTRAINCDIRS += $(ANYKEY_DIR)/pico_shim

# TinyUSB's RP2040 port (tusb_mcu.h) expects pico/platform.h's macros
# (__not_in_flash etc.) to be globally available, which pico-sdk's own CMake
# build normally guarantees. We don't use that build system, so force it in
# for every file compiled here instead - this header is designed to be safe
# to include pervasively in any RP2040 build.
CFLAGS += -include pico/platform.h
