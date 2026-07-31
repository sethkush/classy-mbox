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
