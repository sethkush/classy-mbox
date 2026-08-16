#!/usr/bin/env python3
"""#211 -- ask the feedback endpoint for MORE than 3 bytes and see what it gives.

THE QUESTION. EP 0x82 completes with -EOVERFLOW on 100% of packets and delivers
zero bytes (FINDING_211). EOVERFLOW on an isochronous IN means babble: the
device put more on the wire than the host scheduled for. The host schedules 3
because wMaxPacketSize says 3.

So schedule more. If the device is emitting 8 bytes because EP_BSIZE() cannot
express a buffer smaller than 8, then an 8-byte-per-packet schedule will
SUCCEED and hand back the bytes it is actually sending -- which both confirms
the mechanism and shows whether the 10.14 feedback value inside is correct.

This needs no reflash and cannot change device state: it submits isochronous IN
URBs and reads them. Worst case is that snd-usb-audio needs rebinding, which is
a sysfs write, not a replug.

THE KNOWN-ANSWER ARM IS THE 3-BYTE ROW, and this project's rules require one.
A 3-byte schedule must reproduce the -EOVERFLOW that ALSA already gets. If it
does not, this harness differs from ALSA's in some way that matters and NO row
in the table means anything -- including the interesting ones.

Why raw usbfs rather than pyusb: pyusb has no usable isochronous path. The
ioctl numbers and struct layouts below are x86-64 Linux, checked against
include/uapi/linux/usbdevice_fs.h.

    sudo fbprobe.py --serial RK1672500M
"""
import argparse
import ctypes
import errno
import os
import sys
import time

try:
    import usb.core
    import usb.util
except ImportError:
    sys.exit("pyusb not installed. On the void box: ~/mbox-venv/bin/python")

# --- usbfs ABI ------------------------------------------------------------
USBDEVFS_URB_TYPE_ISO = 0


def _ioc(d, t, nr, size):
    return (d << 30) | (size << 16) | (ord(t) << 8) | nr


# libc.ioctl rather than fcntl.ioctl. fcntl.ioctl cannot express an out-pointer
# argument -- REAPURB hands back a URB POINTER, not a buffer of data -- and its
# attempts to guess produced "buffer overflow" against a correctly sized
# bytearray. Going through libc means the argument is exactly the pointer the
# kernel expects, with no interpretation in between.
_libc = ctypes.CDLL("libc.so.6", use_errno=True)
_libc.ioctl.argtypes = [ctypes.c_int, ctypes.c_ulong, ctypes.c_void_p]
_libc.ioctl.restype = ctypes.c_int


def _io(fd, req, arg=None):
    ctypes.set_errno(0)
    r = _libc.ioctl(fd, req, arg)
    if r < 0:
        raise OSError(ctypes.get_errno(), os.strerror(ctypes.get_errno()))
    return r


class IsoPacketDesc(ctypes.Structure):
    _fields_ = [("length", ctypes.c_uint),
                ("actual_length", ctypes.c_uint),
                ("status", ctypes.c_uint)]


def make_urb(npkts):
    """struct usbdevfs_urb with a flexible iso_frame_desc[] of npkts."""
    class Urb(ctypes.Structure):
        _fields_ = [("type", ctypes.c_ubyte),
                    ("endpoint", ctypes.c_ubyte),
                    ("status", ctypes.c_int),
                    ("flags", ctypes.c_uint),
                    ("buffer", ctypes.c_void_p),
                    ("buffer_length", ctypes.c_int),
                    ("actual_length", ctypes.c_int),
                    ("start_frame", ctypes.c_int),
                    ("number_of_packets", ctypes.c_int),
                    ("error_count", ctypes.c_int),
                    ("signr", ctypes.c_uint),
                    ("usercontext", ctypes.c_void_p),
                    ("iso_frame_desc", IsoPacketDesc * npkts)]
    return Urb


# sizeof(struct usbdevfs_urb) without the flexible member is 56 on x86-64.
USBDEVFS_SUBMITURB = _ioc(2, 'U', 10, 56)
USBDEVFS_REAPURB = _ioc(1, 'U', 12, 8)
USBDEVFS_REAPURBNDELAY = _ioc(1, 'U', 13, 8)
USBDEVFS_DISCARDURB = _ioc(0, 'U', 11, 0)
USBDEVFS_CLAIMINTERFACE = _ioc(2, 'U', 15, 4)
USBDEVFS_RELEASEINTERFACE = _ioc(2, 'U', 16, 4)
USBDEVFS_SETINTERFACE = _ioc(2, 'U', 4, 8)
USBDEVFS_IOCTL = _ioc(3, 'U', 18, 16)
USBDEVFS_DISCONNECT = _ioc(0, 'U', 22, 0)
USBDEVFS_CONNECT = _ioc(0, 'U', 23, 0)


class SetInterface(ctypes.Structure):
    _fields_ = [("interface", ctypes.c_uint), ("altsetting", ctypes.c_uint)]


class DevfsIoctl(ctypes.Structure):
    _fields_ = [("ifno", ctypes.c_int),
                ("ioctl_code", ctypes.c_int),
                ("data", ctypes.c_void_p)]


def driver_op(fd, ifno, code):
    """USBDEVFS_DISCONNECT / _CONNECT, wrapped in USBDEVFS_IOCTL."""
    c = DevfsIoctl(ifno=ifno, ioctl_code=code, data=None)
    try:
        _io(fd, USBDEVFS_IOCTL, ctypes.byref(c))
        return True
    except OSError:
        return False


# --- locating the endpoint ------------------------------------------------

def find_feedback(dev, addr=0x82):
    """-> (bInterfaceNumber, bAlternateSetting, wMaxPacketSize) for `addr`.

    Read from the descriptors rather than hardcoded: EP 0x82's interface is not
    guessable from its number, and getting it wrong claims the wrong interface
    and produces a clean-looking null.
    """
    for cfg in dev:
        for intf in cfg:
            for ep in intf:
                if ep.bEndpointAddress == addr:
                    return (intf.bInterfaceNumber, intf.bAlternateSetting,
                            ep.wMaxPacketSize)
    return None


def run_one(fd, ep, pkt_size, npkts=16, timeout_s=2.0):
    """Submit one iso URB of npkts x pkt_size and reap it.

    -> (status, [(actual_length, status), ...]) or (None, error string)
    """
    Urb = make_urb(npkts)
    buf = (ctypes.c_ubyte * (pkt_size * npkts))()
    u = Urb()
    u.type = USBDEVFS_URB_TYPE_ISO
    u.endpoint = ep
    u.flags = 0
    u.buffer = ctypes.cast(buf, ctypes.c_void_p)
    u.buffer_length = pkt_size * npkts
    u.number_of_packets = npkts
    u.start_frame = 0
    for i in range(npkts):
        u.iso_frame_desc[i].length = pkt_size

    try:
        _io(fd, USBDEVFS_SUBMITURB, ctypes.byref(u))
    except OSError as e:
        return None, f"submit failed: {errno.errorcode.get(e.errno, e.errno)}"

    # Reap. The URB pointer comes back through a void* out-parameter, so the
    # buffer must be MUTABLE and passed with mutate_flag -- fcntl.ioctl will not
    # take a ctypes byref().
    out = ctypes.c_void_p()
    deadline = time.time() + timeout_s
    while True:
        try:
            _io(fd, USBDEVFS_REAPURBNDELAY, ctypes.byref(out))
            break
        except OSError as e:
            if e.errno != errno.EAGAIN:
                return None, f"reap failed: {errno.errorcode.get(e.errno, e.errno)}"
            if time.time() > deadline:
                try:
                    # DISCARDURB takes the pointer VALUE, not a buffer.
                    _io(fd, USBDEVFS_DISCARDURB, ctypes.byref(u))
                    _io(fd, USBDEVFS_REAPURB, ctypes.byref(out))
                except OSError:
                    pass
                return None, "timed out with no completion"
            time.sleep(0.002)

    pkts = [(u.iso_frame_desc[i].actual_length,
             ctypes.c_int(u.iso_frame_desc[i].status).value)
            for i in range(npkts)]
    return u.status, pkts


def summarise(pkts):
    lens = {}
    stats = {}
    for a, s in pkts:
        lens[a] = lens.get(a, 0) + 1
        stats[s] = stats.get(s, 0) + 1
    L = ", ".join(f"{k}B x{v}" for k, v in sorted(lens.items()))
    S = ", ".join(f"{k} x{v}" for k, v in sorted(stats.items()))
    return L, S


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--serial")
    ap.add_argument("--addr", metavar="BUS:ADDR")
    ap.add_argument("--ep", type=lambda x: int(x, 0), default=0x82)
    ap.add_argument("--sizes", default="3,4,8,16",
                    help="per-packet sizes to schedule (3 is the control arm)")
    a = ap.parse_args()

    found = []
    for pid in (0x1000,) + tuple(range(0x2000, 0x2010)):
        for d in usb.core.find(find_all=True, idVendor=0x0DBA, idProduct=pid):
            found.append(d)
    if a.serial:
        def sn(x):
            try:
                return usb.util.get_string(x, x.iSerialNumber) if x.iSerialNumber else None
            except Exception:
                return None
        found = [x for x in found if sn(x) == a.serial]
    if a.addr:
        b, ad = (int(v) for v in a.addr.split(":"))
        found = [x for x in found if (x.bus, x.address) == (b, ad)]
    if len(found) != 1:
        sys.exit(f"need exactly one target, matched {len(found)}")
    dev = found[0]

    loc = find_feedback(dev, a.ep)
    if not loc:
        sys.exit(f"EP 0x{a.ep:02X} is not in any descriptor on this device")
    ifno, alt, wmax = loc
    print(f"EP 0x{a.ep:02X} lives on interface {ifno} alt {alt}, "
          f"wMaxPacketSize {wmax}")
    print(f"device at bus {dev.bus} addr {dev.address}\n")

    path = f"/dev/bus/usb/{dev.bus:03d}/{dev.address:03d}"
    usb.util.dispose_resources(dev)
    fd = os.open(path, os.O_RDWR)

    detached = driver_op(fd, ifno, USBDEVFS_DISCONNECT)
    if detached:
        print(f"detached kernel driver from interface {ifno}")
    try:
        ifc = ctypes.c_uint(ifno)
        _io(fd, USBDEVFS_CLAIMINTERFACE, ctypes.byref(ifc))
        si = SetInterface(interface=ifno, altsetting=alt)
        _io(fd, USBDEVFS_SETINTERFACE, ctypes.byref(si))
        print(f"claimed interface {ifno}, alt {alt}\n")

        print(f"{'sched':>6} {'urb':>6}  {'packet lengths':<28} status counts")
        print("-" * 76)
        for size in [int(s) for s in a.sizes.split(",")]:
            st, pkts = run_one(fd, a.ep, size)
            if st is None:
                print(f"{size:>6} {'--':>6}  {pkts}")
                continue
            L, S = summarise(pkts)
            note = "  <-- CONTROL ARM" if size == wmax else ""
            print(f"{size:>6} {st:>6}  {L:<28} {S}{note}")
    finally:
        try:
            _io(fd, USBDEVFS_RELEASEINTERFACE, ctypes.byref(ctypes.c_uint(ifno)))
        except OSError:
            pass
        # UNCONDITIONAL. An earlier version only reconnected when its own
        # DISCONNECT had succeeded, so a crash between the two left interface 1
        # held by usbfs -- and once held, `echo ... > snd-usb-audio/bind`
        # answers ENODEV, which reads as "no such interface" rather than "you
        # are still holding it". Reconnecting something already connected is
        # harmless; the reverse is not.
        if True:
            # Rebinding is part of the test, not an afterthought -- ch9_probe
            # learned this by leaving a unit unable to capture.
            if driver_op(fd, ifno, USBDEVFS_CONNECT):
                print(f"\ndriver rebound on interface {ifno}")
            else:
                print(f"\nDRIVER NOT REBOUND on interface {ifno}; rebind with:")
                print(f"  echo <busid>:1.{ifno} | sudo tee "
                      f"/sys/bus/usb/drivers/snd-usb-audio/bind")
        os.close(fd)

    print("\nRead the CONTROL ARM row first. If a 3-byte schedule does not")
    print("reproduce the -75 EOVERFLOW that ALSA gets, this harness differs")
    print("from ALSA's and no other row means anything.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
