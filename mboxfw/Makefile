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

CFLAGS  = $(if $(MBOX_PID),-DMBOX_PID=$(MBOX_PID)) -mmcs51 --model-small --std-c99 --opt-code-size $(INC)
# --code-size raised 0x1780 -> 0x1C00 (6016 -> 7168) on 2026-07-30. 0x1780 was
# a margin, never a hardware limit: the 24C64 holds 8192 bytes and an 18-byte
# header, so the real payload ceiling is 8174 -- which is exactly what stock
# Rev 20 occupies. The build hit the old ceiling with 61 bytes to spare while
# adding the host mux control, and a self-imposed limit that blocks diagnostic
# code is costing more than it protects. 7168 keeps 1006 bytes under the hard
# ceiling. tools/check_code_size.py carries the matching budget.
LDFLAGS = -mmcs51 --model-small --code-size 0x1C00 --xram-size 0x1000

SRCS = src/main.c src/hw_init.c src/mux.c src/cs8427.c src/codec.c src/buttons.c src/usb.c src/descriptors.c src/streaming.c src/isr.c src/eeprom.c src/telemetry.c src/power.c
RELS = $(SRCS:src/%.c=$(BUILD)/%.rel)

.PHONY: all clean flasher
all: $(BUILD)/$(TARGET).ihx $(BUILD)/$(TARGET)_flasher.bin
flasher: $(BUILD)/$(TARGET)_flasher.bin

$(BUILD)/$(TARGET)_flasher.bin: $(BUILD)/$(TARGET).ihx
	python3 ../tools/wrap_hex.py $< -o $@

# Objects depend on EVERY header, not just their own .c.
#
# Without this, editing a header silently leaves stale .rel files in the
# build and the linked image does not match the source. On 2026-07-28 that
# shipped an image whose TLM_BUILD_ID still read 0x0002 after the constant
# had been bumped to 0x0003 -- defeating the one counter whose entire job is
# to prove which build is running. A coarse wildcard rebuilds a little more
# than strictly necessary; this firmware takes seconds to compile, and a
# binary that disagrees with its source costs a flash cycle to discover.
HDRS = $(wildcard include/*.h)

$(BUILD)/%.rel: src/%.c $(HDRS) | $(BUILD)
	$(CC) -c $(CFLAGS) $(EXTRA_CFLAGS) -o $@ $<

$(BUILD)/$(TARGET).ihx: $(RELS)
	$(CC) $(LDFLAGS) -o $@ $(RELS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
