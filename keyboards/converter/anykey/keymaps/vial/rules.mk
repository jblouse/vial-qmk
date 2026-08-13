VIA_ENABLE = yes
VIAL_ENABLE = yes
VIAL_INSECURE = yes
RAW_ENABLE = yes

# VIAL_ENABLE defaults these on (build_vial.mk) since Vial's GUI natively
# supports them - real reason we picked Vial over VIA. Not wired up for
# Milestone 1 (no keymap-defined combos/tap-dances/overrides yet); see
# docs/roadmap.md "Beyond Milestone 1".
COMBO_ENABLE = no
TAP_DANCE_ENABLE = no
KEY_OVERRIDE_ENABLE = no

# TEMPORARY Milestone 1 bring-up: real debug console via `qmk console`,
# instead of one-bit send_string() diagnostics. See docs/hardware-notes.md.
VIAL_KEEP_DEBUG_CONSOLE = yes
CONSOLE_ENABLE = yes
