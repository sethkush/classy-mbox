"""Does a FRESH calibration (stock behaviour) reduce the opening transient?

This is the instrument behind FINDING_202's headline number. Stock recalibrates
at EVERY stream open and pays 183 ms of leading zeros for it; #201 calibrates
once per power-up. Measured on unit A, 8 arms each: fresh calibration cuts mean
opening DC ~2.6x, but the PEAK excursion -- which is what the artefact is --
differs by only 2.2 dB (367.6 vs 473.5, i.e. -87.2 vs -85.0 dBFS).

Requires TLM_REQ_DIAG_MODE (0x17), which means "recalibrate at the next stream
open" and is the only way to do that without a power cycle.

Usage:  recal_vs_latched.py <serial> <card> <rounds>

Interleaved, same session, same unit: A = recalibrate before the capture
(request 0x17 clears the latch, so the next stream open calibrates), B = do not.
Interleaved so drift cannot masquerade as an effect, and both orders run.

Known-answer arm: the recalibrated captures MUST show ~8800 lead zeros and the
others MUST show 0. If they do not, the stimulus never fired and the run is void.
"""
import sys, subprocess, wave
import usb.core, numpy as np
serial, card, N = sys.argv[1], sys.argv[2], int(sys.argv[3])
dev = next(d for d in usb.core.find(find_all=True, idVendor=0x0dba) if d.serial_number==serial)

def cap():
    subprocess.run(["arecord","-D","hw:%s"%card,"-f","S24_3LE","-c","2","-r","48000","-d","2","/tmp/rc.wav"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    w=wave.open("/tmp/rc.wav","rb"); fs=w.getframerate(); n=w.getnframes()
    a=np.frombuffer(w.readframes(n),dtype=np.uint8).reshape(n,6).astype(np.int32); w.close()
    v=a[:,0]|(a[:,1]<<8)|(a[:,2]<<16)
    x=np.where(v&0x800000,v-(1<<24),v).astype(np.float64)
    nz=np.nonzero(x)[0]; lead=int(nz[0]) if len(nz) else n
    y=x[lead:]
    return lead, y[:int(.05*fs)].mean(), np.abs(y[:int(.4*fs)]).max()

fresh, stale = [], []
print("  arm            lead   headDC   peak|400ms|")
for i in range(N):
    for tag in (("recal","plain") if i%2==0 else ("plain","recal")):
        if tag=="recal":
            dev.ctrl_transfer(0x40, 0x17, 0, 0, None, 2000)
        lead, dc, pk = cap()
        (fresh if tag=="recal" else stale).append((lead, dc, pk))
        print("  %-8s %8d %+9.1f %10.1f" % (tag, lead, dc, pk))

for name, arr in (("RECALIBRATED (stock)", fresh), ("latched (0x0053)", stale)):
    dc = np.array([a[1] for a in arr]); pk = np.array([a[2] for a in arr])
    ld = np.array([a[0] for a in arr])
    print("%-22s n=%d  lead %5.0f  |headDC| mean %7.1f  peak mean %7.1f (%.1f dBFS)"
          % (name, len(arr), ld.mean(), np.abs(dc).mean(), pk.mean(),
             20*np.log10(pk.mean()/8388608.0)))
