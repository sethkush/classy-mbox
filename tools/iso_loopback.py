#!/usr/bin/env python3
"""
#171 -- what do IRAM 0x23.2 / 0x23.3 (the "mute pair") actually do?

Single boot, two arms, one variable:

  arm A : stream with the pair LOW  (no SET_CUR has been sent since boot)
  arm B : send SET_CUR(48000), which is the ONLY writer that raises the pair,
          then stream again

Everything runs on one libusb handle -- telemetry, setmux, SET_CUR and both
isochronous streams -- so nothing races for the device and each arm can state
its own pair state instead of having it assumed.

Requires the boot state to be uncontaminated: snd-usb-audio must be blocked
from loading (it issues SET_CUR on bind and would raise the pair before we
look). Verified by asserting pair-low at the start.

Playback tone goes on channel 2 only, because the bench loopback is
A out2 -> A src2 (BENCH_WIRING.md). Channel 1 is therefore a built-in control:
it should sit at the noise floor in both arms.
"""
import sys, math, struct, time
import usb1

VID, PID = 0x0dba, 0x2000
EP_IN, EP_OUT = 0x81, 0x02

RATE        = 48000
TONE_HZ     = 1000.0
FRAMES_PKT  = 48                    # 1 ms at 48 kHz
CH          = 2
SUBFRAME    = 3                     # 24-bit
PKT_BYTES   = FRAMES_PKT * CH * SUBFRAME    # 288
ISO_PKTS    = 32                    # packets per transfer
N_XFER      = 8                     # transfers in flight per direction
RUN_SEC     = 2.0

TLM_READ    = 0x10
TLM_SET_MUX = 0x13
MUX_LINE    = 0x05


# ---------- helpers ----------------------------------------------------

def s24le(x):
    v = int(max(-1.0, min(1.0, x)) * 0x7FFFFF) & 0xFFFFFF
    return bytes((v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF))


def parse_s24le(buf):
    """-> (ch1[], ch2[]) as floats in -1..1"""
    c1, c2 = [], []
    n = len(buf) // (CH * SUBFRAME)
    for i in range(n):
        o = i * CH * SUBFRAME
        for ch, dst in ((0, c1), (1, c2)):
            b = buf[o + ch * SUBFRAME: o + ch * SUBFRAME + SUBFRAME]
            v = b[0] | (b[1] << 8) | (b[2] << 16)
            if v & 0x800000:
                v -= 0x1000000
            dst.append(v / float(0x7FFFFF))
    return c1, c2


def goertzel(samples, freq, rate):
    """Magnitude at freq, normalised by sample count."""
    n = len(samples)
    if n == 0:
        return 0.0
    k = int(0.5 + (n * freq) / rate)
    w = (2.0 * math.pi * k) / n
    cw, sw = math.cos(w), math.sin(w)
    coeff = 2.0 * cw
    s0 = s1 = s2 = 0.0
    for x in samples:
        s0 = x + coeff * s1 - s2
        s2, s1 = s1, s0
    real = s1 - s2 * cw
    imag = s2 * sw
    return math.sqrt(real * real + imag * imag) / (n / 2.0)


def rms(samples):
    if not samples:
        return 0.0
    return math.sqrt(sum(x * x for x in samples) / len(samples))


def dbfs(x):
    return 20.0 * math.log10(x) if x > 1e-12 else -240.0


# ---------- device ------------------------------------------------------

def tlm(handle, block):
    return handle.controlRead(0xC0, TLM_READ, block, 0, 8, timeout=1000)


def pair_state(handle):
    b = tlm(handle, 9)
    word = (b[2] << 8) | b[3]
    return word, b[2], bool(b[2] & 0x0C)


def set_mux_line(handle):
    wv = MUX_LINE | (MUX_LINE << 3)
    handle.controlWrite(0x40, TLM_SET_MUX, wv, 0, b'', timeout=1000)


def set_cur_rate(handle, rate):
    data = bytes((rate & 0xFF, (rate >> 8) & 0xFF, (rate >> 16) & 0xFF))
    # bmRequestType 0x22 = host->device, class, ENDPOINT recipient
    # wValue high byte = SAMPLING_FREQ_CONTROL (0x01)
    handle.controlWrite(0x22, 0x01, 0x0100, EP_OUT, data, timeout=1000)


def build_tone():
    """One transfer's worth of tone: ch2 carries it, ch1 is silent."""
    buf = bytearray()
    phase = 0.0
    step = 2.0 * math.pi * TONE_HZ / RATE
    for _ in range(ISO_PKTS * FRAMES_PKT):
        s = 0.5 * math.sin(phase)
        phase += step
        buf += s24le(0.0)   # ch1 -- control, stays silent
        buf += s24le(s)     # ch2 -- loopback out2 -> src2
    return bytes(buf)


def run_arm(ctx, handle, label):
    """Stream for RUN_SEC, return (ch1, ch2) captured sample lists."""
    tone = build_tone()
    captured = bytearray()
    stop = [False]

    def on_in(t):
        if stop[0]:
            return False
        if t.getStatus() == usb1.TRANSFER_COMPLETED:
            for i, pkt in enumerate(t.iterISO()):
                st, data = pkt
                if st == 0 and data:
                    captured.extend(data)
        try:
            t.submit()
        except Exception:
            return False
        return True

    def on_out(t):
        if stop[0]:
            return False
        try:
            t.submit()
        except Exception:
            return False
        return True

    xfers = []
    for _ in range(N_XFER):
        t = handle.getTransfer(iso_packets=ISO_PKTS)
        t.setIsochronous(EP_OUT, tone, callback=on_out)
        t.submit()
        xfers.append(t)
    for _ in range(N_XFER):
        t = handle.getTransfer(iso_packets=ISO_PKTS)
        t.setIsochronous(EP_IN, ISO_PKTS * PKT_BYTES, callback=on_in)
        t.submit()
        xfers.append(t)

    t_end = time.time() + RUN_SEC
    while time.time() < t_end:
        ctx.handleEventsTimeout(0.1)
    stop[0] = True
    for t in xfers:
        try:
            t.cancel()
        except Exception:
            pass
    deadline = time.time() + 1.0
    while time.time() < deadline:
        ctx.handleEventsTimeout(0.05)

    c1, c2 = parse_s24le(bytes(captured))
    print(f"  [{label}] captured {len(captured)} B -> {len(c1)} frames")

    # A stream that delivered nothing is NOT silence -- it is a stream that
    # never ran, and the two are indistinguishable in the numbers. Treating
    # no-data as -240 dBFS produced a confident and completely wrong verdict
    # on 2026-08-04 ("the pair is a mute"), when the real cause was that
    # SET_CUR is the only thing that programs the ACG: without it there is no
    # MCLKO, the codec is unclocked, and the UBM answers every IN token with a
    # NULL packet. Refuse to report rather than measure the wrong thing.
    if len(c1) < RATE // 10:      # < 100 ms of audio
        raise SystemExit(
            f"ABORT: arm {label} captured only {len(c1)} frames. That is a dead\n"
            "stream, not a quiet one, and no comparison drawn from it is valid.\n"
            "SET_CUR raises the pair AND programs the clock in one request, so\n"
            "the pair cannot be isolated from the host. It needs a build in\n"
            "which streaming_set_rate() omits `g_codec_state_23 |= 0x0C`.")
    return c1, c2


def report(label, word, c1, c2):
    m1, m2 = goertzel(c1, TONE_HZ, RATE), goertzel(c2, TONE_HZ, RATE)
    r1, r2 = rms(c1), rms(c2)
    print(f"\n  --- {label} --- codec word = 0x{word:04X}  "
          f"pair {'HIGH' if word & 0x0C00 else 'LOW'}")
    print(f"    ch1 (control, no tone): 1kHz {dbfs(m1):7.2f} dBFS   "
          f"rms {dbfs(r1):7.2f} dBFS")
    print(f"    ch2 (looped, tone in ): 1kHz {dbfs(m2):7.2f} dBFS   "
          f"rms {dbfs(r2):7.2f} dBFS")
    return m2, r2


def main():
    with usb1.USBContext() as ctx:
        handle = ctx.openByVendorIDAndProductID(VID, PID)
        if handle is None:
            sys.exit("no 0dba:2000 found")
        try:
            handle.setAutoDetachKernelDriver(True)
        except Exception:
            pass

        word, hi, pair = pair_state(handle)
        print(f"initial codec word = 0x{word:04X}  pair={'HIGH' if pair else 'LOW'}")
        if pair:
            sys.exit("ABORT: pair is already HIGH -- boot state contaminated.\n"
                     "Block snd-usb-audio autoload and power-cycle first:\n"
                     '  echo "install snd_usb_audio /bin/true" '
                     "| sudo tee /etc/modprobe.d/zz-mbox-test.conf\n"
                     "  sudo modprobe -r snd_usb_audio   # then replug")

        set_mux_line(handle)
        word, hi, pair = pair_state(handle)
        print(f"after setmux line line: 0x{word:04X}  "
              f"pair={'HIGH' if pair else 'LOW'} (setmux must NOT raise it)")
        if pair:
            sys.exit("ABORT: setmux raised the pair -- experiment invalid")

        for iface in (1, 2):
            handle.claimInterface(iface)
            handle.setInterfaceAltSetting(iface, 1)

        print("\nARM A -- streaming with the pair LOW")
        a1, a2 = run_arm(ctx, handle, "A")
        wa, _, pa = pair_state(handle)
        if pa:
            print("  WARNING: pair rose during arm A")
        ma, ra = report("ARM A  pair LOW", wa, a1, a2)

        print("\nsending SET_CUR(48000) -- the single writer that raises the pair")
        set_cur_rate(handle, RATE)
        time.sleep(0.2)
        wb, _, pb = pair_state(handle)
        print(f"  codec word now 0x{wb:04X}  pair={'HIGH' if pb else 'LOW'}")
        if not pb:
            sys.exit("ABORT: SET_CUR did not raise the pair -- nothing to compare")

        print("\nARM B -- streaming with the pair HIGH")
        b1, b2 = run_arm(ctx, handle, "B")
        wb2, _, _ = pair_state(handle)
        mb, rb = report("ARM B  pair HIGH", wb2, b1, b2)

        for iface in (1, 2):
            try:
                handle.setInterfaceAltSetting(iface, 0)
                handle.releaseInterface(iface)
            except Exception:
                pass

        print("\n================ VERDICT ================")
        print(f"  ch2 1 kHz  pair LOW  {dbfs(ma):7.2f} dBFS")
        print(f"  ch2 1 kHz  pair HIGH {dbfs(mb):7.2f} dBFS")
        d = dbfs(mb) - dbfs(ma)
        print(f"  delta                {d:+7.2f} dB")
        if d > 20:
            print("  => pair HIGH lets audio through, LOW does not."
                  "\n     The pair is an output mute / audio-path enable. READING CONFIRMED.")
        elif abs(d) < 6:
            print("  => no material difference. The pair is NOT an output mute"
                  "\n     on this path. The mute reading is NOT supported.")
        else:
            print("  => partial/ambiguous. Do not conclude; re-run and inspect.")


if __name__ == "__main__":
    main()
