"""When inside the tRTV window does a source step stop reaching the output?

Supersedes an earlier version whose alternation was broken: it chose the target
source from the loop index rather than from the CURRENT source, so after the
first iteration the "test" switch was usually a no-op -- setting MIC when the mux
was already MIC. The averages were then one real step diluted by eleven flat
traces, which read as huge suppression. The +230 ms arm is the control that
caught it: it lands after SDATA is valid and MUST look like an ordinary live
step, and it did not.

Here the source is tracked explicitly, so every switch is a real transition.
"""
import sys, time, subprocess, usb.core
MIC, LINE = 0x06, 0x05
serial, card, rate, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
DELAYS = [0.010, 0.050, 0.090, 0.130, 0.170, 0.230, 0.400]
REPS = 4
dev = next((d for d in usb.core.find(find_all=True, idVendor=0x0dba)
            if d.serial_number == serial), None)
if dev is None: sys.exit("no device %s" % serial)
def setmux(p): dev.ctrl_transfer(0x40, 0x13, p | (p << 3), 0, None, 1000)
def setclk():  dev.ctrl_transfer(0x40, 0x14, 2 if rate == 48000 else 1, 0xFF, None, 2000)

cur = MIC
setmux(cur); time.sleep(0.5)
seq = [(d, i) for d in DELAYS for i in range(REPS)]
rec = subprocess.Popen(["arecord","-D","hw:%s"%card,"-f","S24_3LE","-c","2",
                        "-r",str(rate),"-d",str(2+len(seq)*8),out],
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
t0=time.time()
def at(t):
    time.sleep(max(0.0, t0+t-time.time()))
dirs = []
for nn,(dly,i) in enumerate(seq):
    b = 2.0 + nn*8.0
    at(b);       setclk()
    at(b+dly);   nxt = LINE if cur == MIC else MIC; setmux(nxt)
    dirs.append(1 if nxt == LINE else -1); cur = nxt          # TEST step
    at(b+4.0);   nxt = LINE if cur == MIC else MIC; setmux(nxt); cur = nxt   # COMPARATOR
print("DIRS " + "".join("+" if d>0 else "-" for d in dirs))
rec.wait(); print("wrote", out)
