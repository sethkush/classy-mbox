"""EP0 chunked-path health check -- does a multi-packet reply lose packets?

Reads the config descriptor N times and compares tlm.chunks/tlm.drains growth
against the floor. This is the ONLY path tlm.chunks counts: single-packet
replies go through stage_immediate(), which writes the EP0 buffer directly and
never increments it.

That distinction is why mboxtlm.py used to print "pushes are being missed" on
every single reading -- every telemetry read widens iep0-vs-chunks by one, as
does every no-data control write. The gap is meaningless; this is the test
that is not.

Measured 2026-08-11, unit B: 50 reads of the 238-byte config descriptor, 0
short reads, chunks +1516 against a floor of +1500.

Usage:  ep0_chunked_health.py <serial>
"""
import sys, math
import usb.core
serial = sys.argv[1]
dev = next(d for d in usb.core.find(find_all=True, idVendor=0x0dba) if d.serial_number==serial)
def blk1():
    b = bytes(dev.ctrl_transfer(0xC0, 0x10, 1, 0, 8, 2000))
    u = lambda i: b[i] | (b[i+1] << 8)
    return u(0), u(2), u(4), u(6)
L = len(dev.ctrl_transfer(0x80, 0x06, 0x0200, 0, 255, 3000))
pk = math.ceil(L/8); N = 50
s0,i0,c0,d0 = blk1()
short = 0
for _ in range(N):
    if len(dev.ctrl_transfer(0x80, 0x06, 0x0200, 0, 255, 3000)) != L:
        short += 1
s1,i1,c1,d1 = blk1()
print("descriptor %d B = %d packets;  %d reads" % (L, pk, N))
print("  short/truncated reads : %d   (any non-zero = real loss)" % short)
print("  chunks  +%d   (minimum expected +%d)" % (c1-c0, N*pk))
print("  drains  +%d   (minimum expected +%d)" % (d1-d0, N))
print("  iep0    +%d" % (i1-i0))
print()
print("VERDICT:", "healthy -- every byte delivered, chunk count at or above the floor"
      if short==0 and c1-c0 >= N*pk and d1-d0 >= N else "REAL LOSS -- investigate")
