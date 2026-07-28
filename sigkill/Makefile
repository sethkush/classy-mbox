# sigkill — minimal EEPROM-signature eraser. See src/main.c for the why.
#
# Built to be RUN from program RAM via the boot ROM's DFU_TARGET_RAM path
# (real SDA short), not flashed. dataType in the header is irrelevant in RAM
# mode, but 0x01 keeps the image valid for the flasher's static checks.

BUILD  = build
TARGET = sigkill

CFLAGS  = -mmcs51 --model-small --std-c99 --opt-code-size
# 6016 bytes is the real program RAM size (TAS1020B datasheet, stated twice).
LDFLAGS = -mmcs51 --model-small --code-size 0x1780 --xram-size 0x1000

.PHONY: all clean
all: $(BUILD)/$(TARGET)_flasher.bin

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.rel: src/main.c | $(BUILD)
	sdcc -c $(CFLAGS) -o $@ $<

$(BUILD)/$(TARGET).ihx: $(BUILD)/main.rel
	sdcc $(LDFLAGS) -o $@ $<

$(BUILD)/$(TARGET)_flasher.bin: $(BUILD)/$(TARGET).ihx
	python3 ../tools/wrap_hex.py $< -o $@

clean:
	rm -rf $(BUILD)
