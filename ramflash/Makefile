# ramflash — writes a full firmware image to EEPROM from program RAM.
# See src/main.c for why the boot ROM's own EEPROM writer is bypassed.
#
# RUN from program RAM via the boot ROM's DFU_TARGET_RAM path (real SDA
# short), never flashed. --attribute 0x00 clears EEPROM_HEADER_OVERWRITE
# so the boot ROM does not attempt an I2C header write on block 0 — that
# write is exactly the thing that is broken, and a RAM target has no use
# for it.

BUILD  = build
TARGET = ramflash

CFLAGS  = -mmcs51 --model-small --std-c99 --opt-code-size
# 6016 bytes is the real program RAM size (TAS1020B datasheet, stated twice).
LDFLAGS = -mmcs51 --model-small --code-size 0x1780 --xram-size 0x1000

.PHONY: all clean
all: $(BUILD)/$(TARGET)_flasher.bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.rel: src/main.c src/payload.h | $(BUILD)
	sdcc -c $(CFLAGS) -Isrc -o $@ $<

$(BUILD)/payload.rel: src/payload.c src/payload.h | $(BUILD)
	sdcc -c $(CFLAGS) -Isrc -o $@ $<

$(BUILD)/$(TARGET).ihx: $(BUILD)/main.rel $(BUILD)/payload.rel
	sdcc $(LDFLAGS) -o $@ $(BUILD)/main.rel $(BUILD)/payload.rel

$(BUILD)/$(TARGET)_flasher.bin: $(BUILD)/$(TARGET).ihx
	python3 ../tools/wrap_hex.py $< -o $@ --attribute 0x00

clean:
	rm -rf $(BUILD)
