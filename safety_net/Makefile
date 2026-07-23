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
all: $(BUILD)/$(TARGET).ihx $(BUILD)/$(TARGET)_flasher.bin

$(BUILD)/$(TARGET)_flasher.bin: $(BUILD)/$(TARGET).ihx
	python3 ../tools/wrap_hex.py $< -o $@

$(BUILD)/%.rel: src/%.c | $(BUILD)
	$(CC) -c $(CFLAGS) -o $@ $<

$(BUILD)/$(TARGET).ihx: $(RELS)
	$(CC) $(LDFLAGS) -o $@ $(RELS)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
