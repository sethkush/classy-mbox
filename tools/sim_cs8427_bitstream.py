# SUPERSEDED 2026-07-31 by tools/sim_p1_waveform.py. KEPT AS THE RECORD OF A
# DEFECT, NOT AS A CHECK OF THE CURRENT FIRMWARE.
#
# This file HAND-MODELS the cs8427.c that existed before #157 -- the I2C-framed
# version with START/STOP and an ACK slot, and with the chip select never
# driven. That code is gone. Nothing below reads the firmware, so nothing below
# will notice when the firmware changes again; its "stock" arm is a hand-model
# too, not the stock image.
#
# sim_p1_waveform.py replaces both halves with measurement: it executes the
# real image in ucSim, decodes the actual P1 waveform, and validates the
# decoder by running Rev 20 and Rev 22 through it. Use that. This stays so the
# 281-bit number quoted in cs8427.c's header can still be reproduced.
#
# Simulate what mboxfw's I2C-framed bit-bang actually shifts into the CS8427's
# SPI control port, given the chip select (IRAM 0x25.7) is held ASSERTED (low)
# for the entire life of the firmware because g_codec_state_25 == 0 and nothing
# ever sets bit 7.
#
# CS8427 SPI (DS477F5 s9.1): data clocked in on the RISING edge of CCLK.
# One CS-low period = one continuous shift: byte0 = chip addr + R/W,
# byte1 = MAP (bit7 = INCR), every following byte = data.

SCL, SDA = 0x08, 0x10
p1 = 0x00          # hw_init: P1 = 0x00
bits = []

def w(newp1):
    global p1
    rising = (not (p1 & SCL)) and (newp1 & SCL)
    p1 = newp1
    if rising:
        bits.append(1 if p1 & SDA else 0)

def start():
    w(p1 | SCL | SDA); w(p1 & ~SDA); w(p1 & ~SCL)

def shift(b):
    for i in range(8):
        w((p1 | SDA) if (b & 0x80) else (p1 & ~SDA))
        w(p1 | SCL); w(p1 & ~SCL)
        b = (b << 1) & 0xFF
    w(p1 | SDA); w(p1 | SCL); w(p1 & ~SCL)      # ACK slot -> a 1 bit

def stop():
    w(p1 & ~SDA); w(p1 | SCL); w(p1 | SDA)      # -> a 0 bit, SCL left HIGH

seq = [(0x04,0x00),(0x13,0x10),(0x04,0x00),(0x04,0x40),(0x01,0x01),
       (0x02,0x20),(0x03,0x0C),(0x05,0x05),(0x06,0x05),(0x11,0xFF)]
for reg,val in seq:
    start(); shift(0x20); shift(reg); shift(val); stop()

print(f"total bits clocked into the CS8427: {len(bits)}")
by = [int("".join(map(str,bits[i:i+8])),2) for i in range(0,len(bits)-7,8)]
print("bytes:", " ".join(f"{b:02x}" for b in by))
print()
addr, mapb = by[0], by[1]
print(f"byte0 (chip addr + R/W) = 0x{addr:02x}  -> expected 0x20; {'MATCH' if addr==0x20 else 'MISMATCH'}")
print(f"byte1 (MAP)             = 0x{mapb:02x}  INCR={'1 (auto-increment)' if mapb&0x80 else '0 (fixed)'}  start reg=0x{mapb&0x7f:02x}")
print()
regs = {}
m = mapb & 0x7f
for b in by[2:]:
    regs[m] = b
    if mapb & 0x80:
        m = (m + 1) & 0x7f
print("resulting CS8427 register contents:")
NAMES={0x01:"CONTROL1",0x02:"CONTROL2",0x03:"DATAFLOW",0x04:"CLOCKSOURCE",
       0x05:"SERIALINPUT",0x06:"SERIALOUTPUT",0x11:"RECVERRMASK",0x13:"UDATABUF"}
INTENDED=dict(seq)
for r in sorted(regs):
    n=NAMES.get(r,f"reg{r:02x}")
    want=INTENDED.get(r)
    tag = "" if want is None else ("  (intended 0x%02x)"%want + ("  OK" if want==regs[r] else "  WRONG"))
    print(f"  0x{r:02x} {n:<13} = 0x{regs[r]:02x}{tag}")
print()
cs = regs.get(0x04)
if cs is not None:
    print(f"CLOCKSOURCE (0x04) = 0x{cs:02x}  RUN(bit6) = {(cs>>6)&1}")
so = regs.get(0x06)
if so is not None:
    print(f"SERIALOUTPUT (0x06) = 0x{so:02x}  SOMS(bit7)={(so>>7)&1} SORES={(so>>4)&3} SODEL={(so>>2)&1}")

# ---------------------------------------------------------------------------
# VALIDATION: run stock's routine through the same CS8427-side decoder.
# If the decoder is right, stock must decode to (0x20, reg, value) exactly.
# Stock (Rev 20 0x0C45): RL A then test ACC.0 == MSB-first; one rising SCL
# edge per bit; 8 bits per byte with NO ack slot; CS framed around all three.
# ---------------------------------------------------------------------------
def stock_stream(reg, value):
    out = []
    for byte in (0x20, reg, value):
        b = byte
        for _ in range(8):
            b = ((b << 1) | (b >> 7)) & 0xFF     # RL A
            out.append(b & 1)                    # JNB ACC.0
    return out

fails = 0
for reg, val in seq:
    s = stock_stream(reg, val)
    by = [int("".join(map(str, s[i:i+8])), 2) for i in range(0, 24, 8)]
    ok = by == [0x20, reg, val]
    if not ok:
        fails += 1
        print(f"  STOCK DECODE FAIL reg=0x{reg:02x}: {[hex(x) for x in by]}")
assert fails == 0, "decoder model rejected by stock's own bit stream"
print("validation: stock's stream decodes to (0x20, MAP, data) for all 10 writes -- decoder model OK")
