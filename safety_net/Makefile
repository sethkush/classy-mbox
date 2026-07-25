# Safety-net firmware — minimum-viable Mbox 1 image for proving the
# flash toolchain and recovery paths.
#
# Produces build/safety_net.ihx and build/safety_net_flasher.bin. Flash
# with the SAME mboxflash + wrap_hex used for the real firmware.

CC      = sdcc
TARGET  = safety_net
BUILD   = build
CFLAGS  = -mmcs51 --model-small --std-c99 --opt-code-size
LDFLAGS = -mmcs51 --model-small --code-size 0x1F00 --xram-size 0x1000

SRCS = src/main.c
RELS = $(SRCS:src/%.c=$(BUILD)/%.rel)

.PHONY: all clean
all: $(BUILD)/$(TARGET).ihx $(BUILD)/$(TARGET)_flasher.bin $(BUILD)/$(TARGET)_bootstrap.bin

# Normal flasher — dataType = 0x01 (APPCODE). Boot ROM will try to run
# this code directly on the next power cycle. Only usable when device is
# in app-DFU mode (0x0DBA:0x1001) which actually persists code to EEPROM.
$(BUILD)/$(TARGET)_flasher.bin: $(BUILD)/$(TARGET).ihx
	python3 ../tools/wrap_hex.py $< -o $@

# Bootstrap flasher — dataType = 0x03 (APPCODE_UPDATING). Only the header
# gets persisted (bulletproof DFU only writes headers), and the 0x03
# dataType tells the boot ROM on next power-up: "flash in progress, come
# up in app-DFU (0x0DBA:0x1001)". THEN we can flash the normal artifact
# above, and its code will actually persist to EEPROM.
#
# Why this is necessary: bulletproof DFU (UsbDfu.c:948-951) writes CODE
# to RAM only, not EEPROM. Flashing the normal 0x01 artifact from
# bulletproof leaves the chip with a valid header pointing at garbage —
# boot ROM jumps to garbage on replug (all lights on, then silent).
# Fixed 2026-07-23 after two bricks caused by exactly this.
$(BUILD)/$(TARGET)_bootstrap.bin: $(BUILD)/$(TARGET).ihx
	python3 ../tools/wrap_hex.py $< -o $@ --data-type 0x03

$(BUILD)/%.rel: src/%.c | $(BUILD)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD)/$(TARGET).ihx: $(RELS)
	$(CC) $(LDFLAGS) -o $@ $(RELS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
