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

# #46 EXPERIMENT ONLY -- `make MBOX_MUTE_PAIR_MASK=0x04` (or 0x08) raises only
# ONE of the two bits. #171 settled that the PAIR gates both directions, but it
# only ever varied them together -- the 0x001E build removed both. Whether
# 0x23.2 and 0x23.3 are one gate or two (playback and capture separately) is
# still open, and it is the whole question for a UAC Feature Unit: a Mute
# control is honest only if muting one direction leaves the other alone.
#
# Costs nothing at runtime -- it is a compile-time constant folded into the one
# `orl` -- so the answer needs a flash, not a code change. Reports itself as
# TLM_BUILD_ID 0x0025 (0x04, playback candidate) or 0x0026 (0x08).
#
# SUPERSEDED for #189 by TLM_REQ_SET_MUTE (telemetry.h), which moves the same
# two bits at RUNTIME. Prefer it: two images cost two power cycles and two 2 km
# round trips to produce one A/B, and give each mask value exactly one attempt
# with a reflash between the halves of the comparison. The request reaches all
# four states on one power cycle, on one unit, repeatably.
#
# This switch is kept because it sets the mask the device BOOTS with, which the
# request cannot -- if the question ever becomes what state the audio path comes
# up in, rather than what the bits gate, this is the instrument for it.
MBOX_MUTE_PAIR_MASK ?=
ifneq ($(MBOX_MUTE_PAIR_MASK),)
EXTRA_CFLAGS += -DCODEC23_MUTE_PAIR=$(MBOX_MUTE_PAIR_MASK)
ifeq ($(MBOX_MUTE_PAIR_MASK),0x04)
EXTRA_CFLAGS += -DTLM_BUILD_ID=0x0025
endif
ifeq ($(MBOX_MUTE_PAIR_MASK),0x08)
EXTRA_CFLAGS += -DTLM_BUILD_ID=0x0026
endif
endif

# Per-unit serial number. `make MBOX_UNIT=A` / `=B` serves iSerialNumber with
# the serial printed on that unit, so BOTH units can build at the same
# MBOX_PID and still be addressed individually by the host. Undefined = no
# serial descriptor and iSerialNumber 0, exactly as before. See usb.h.
ifeq ($(MBOX_UNIT),A)
EXTRA_CFLAGS += -DMBOX_SERIAL_A
endif
ifeq ($(MBOX_UNIT),B)
EXTRA_CFLAGS += -DMBOX_SERIAL_B
endif

# RELEASE BUILD. `make MBOX_RELEASE=1` compiles out the diagnostic surface:
# the telemetry block reader and counter reset, the saturating counters
# themselves, the boot canaries, the stage ladder, the last-SETUP capture, and
# the mux / clock / recalibrate vendor aliases. See mboxfw/RELEASE.md.
#
# NOT stripped, and this is deliberate in both cases:
#   * TLM_REQ_ENTER_DFU  -- the ONLY software path into DFU. The class-request
#     form is interface-recipient and undeliverable once a host driver binds,
#     which on a release build is always. Without this the device cannot be
#     reflashed without opening the case.
#   * the per-unit serial (MBOX_UNIT) -- a FEATURE, not scaffolding. macOS keys
#     per-device settings on iSerialNumber, so two units without serials collide.
#     A release image is therefore built PER UNIT.
ifeq ($(MBOX_RELEASE),1)
EXTRA_CFLAGS += -DMBOX_RELEASE
endif

# LEAN DIAGNOSTIC BUILD. `make MBOX_TLM_LEAN=1` keeps telemetry blocks 0 and 9
# and drops 1 and 2, so a feature-heavy build still answers the two questions
# every bench session actually asks -- "which build is running" (block 0) and
# "what is this capture routed to" (block 9, the one whose absence voided a whole
# session on 2026-07-29).
#
# This exists so features never have to be release-only. A feature present in the
# shipped image but absent from the build that carries telemetry would be both
# untested AND undiagnosable, which inverts the reason telemetry exists at all:
# one power cycle buys one image and costs a 2 km round trip.
#
# Blocks 1 and 2 are the droppable pair. Block 1's defect is fixed and its
# warning was measured to be an accounting artefact (FINDING: chunks counts the
# chunked path only); block 2 is generic SETUP debug that matters when
# enumeration is broken, which is not the state a feature build is usually in.
ifeq ($(MBOX_TLM_LEAN),1)
EXTRA_CFLAGS += -DMBOX_TLM_LEAN
endif

CFLAGS  = $(if $(MBOX_PID),-DMBOX_PID=$(MBOX_PID)) -mmcs51 --model-small --std-c99 --opt-code-size $(INC)
# --code-size REVERTED 0x1C00 -> 0x1780 (7168 -> 6016) on 2026-08-03, after an
# image built against the raised value bricked Mbox A.
#
# 0x1780 IS A HARDWARE LIMIT. The TAS1020B has 6016 bytes (0x1780) of program
# RAM -- TAS1020B datasheet, features list and §1 overview, stated twice; see
# ramloader/DESIGN.md "Verified constraints". The boot ROM copies the EEPROM
# payload into that RAM and runs it there. An image larger than 6016 bytes
# cannot exist on this part, however much EEPROM is behind it.
#
# The 2026-07-30 raise argued the limit was imaginary because "stock Rev 20
# occupies 8174 bytes -- nearly the full EEPROM". That is padding, not code.
# Stock Rev 20's last non-0xFF byte is at 0x103E: 4159 bytes of real content,
# and Rev 22 is 4150. Both sit ~1850 bytes UNDER 6016. The 8174 figure is the
# FF-filled EEPROM region, and reading it as code is what removed the guard.
#
# ramloader/DESIGN.md predicted this exact failure while the build was 3399 B:
# "the linker will happily produce an image that cannot exist." On 2026-08-03
# mboxfw reached 6448 bytes, passed every gate, flashed cleanly, reached DFU
# manifest -- and came up silent on USB, because 432 bytes of it had nowhere
# to live. See BRICK_LOG.md.
#
# safety_net/Makefile was never raised and still links at 0x1780. That is why
# it remains flashable and is the recovery image.
LDFLAGS = -mmcs51 --model-small --code-size 0x1780 --xram-size 0x1000

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
