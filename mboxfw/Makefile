# Mbox 1 custom firmware — SDCC/mcs51 build.
#
# Produces build/mboxfw.ihx (Intel HEX), which needs to be re-wrapped in
# Digi's TI Intel-HEX record format for mboxflash to push to EEPROM.
# See tools/wrap_hex.py (TODO).

CC      = sdcc
TARGET  = mboxfw
BUILD   = build
INC     = -Iinclude
# LED progress canary. `make CANARY_LED=1` builds the diagnostic image
# that blinks how far execution reached on the front panel; the default
# build is unchanged. See the ladder in src/main.c.
CANARY_LED ?= 0
ifeq ($(CANARY_LED),1)
EXTRA_CFLAGS += -DCANARY_LED
endif

CFLAGS  = -mmcs51 --model-small --std-c99 --opt-code-size $(INC)
LDFLAGS = -mmcs51 --model-small --code-size 0x1780 --xram-size 0x1000

SRCS = src/main.c src/hw_init.c src/mux.c src/cs8427.c src/codec.c src/buttons.c src/usb.c src/descriptors.c src/streaming.c src/isr.c src/eeprom.c
RELS = $(SRCS:src/%.c=$(BUILD)/%.rel)

.PHONY: all clean flasher
all: $(BUILD)/$(TARGET).ihx $(BUILD)/$(TARGET)_flasher.bin
flasher: $(BUILD)/$(TARGET)_flasher.bin

$(BUILD)/$(TARGET)_flasher.bin: $(BUILD)/$(TARGET).ihx
	python3 ../tools/wrap_hex.py $< -o $@

$(BUILD)/%.rel: src/%.c | $(BUILD)
	$(CC) -c $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $<

$(BUILD)/$(TARGET).ihx: $(RELS)
	$(CC) $(LDFLAGS) -o $@ $(RELS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
