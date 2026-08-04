#!/usr/bin/env python3
"""
Static reachability check on the compiled main.rst:

  Property: on any execution path from main() entry that does NOT
  invoke check_boot_dfu_button's early `ljmp 0` warm-reset, usb_init
  is called EXACTLY ONCE before the polling loop entry.

Justification: usb_init ends with USBCTL |= USBCTL_CONN. If it's ever
skipped (bug: conditional guard, wrong call order, dead-code removal
by a future refactor), the device never attaches to the bus and the
"never brick" guarantee falls apart.

This is a lightweight version of the "USBCTL CONN bit is set on all
paths" formal property. Full symbolic execution of 8051 is out of
scope; scanning main.rst for the specific pattern is enough to catch
the bug class we care about.
"""

import re
import sys
from pathlib import Path


RST = Path("mboxfw/build/main.rst")


def scan_main_body() -> list[tuple[int, str]]:
    """Return [(rst_line_num, mnemonic_line), ...] between _main: and
    the sjmp back at the end of the polling loop."""
    lines = RST.read_text().splitlines()
    inside = False
    body = []
    for i, raw in enumerate(lines, start=1):
        if "_main:" in raw and "_main_p" not in raw:
            inside = True
            continue
        if not inside:
            continue
        body.append((i, raw))
        # main ends at the sjmp back to the top of the for(;;) — after
        # that we're in someone else's function body.
        if "sjmp" in raw.lower() and re.search(r"sjmp\s+0010\d\$", raw):
            break
    return body


def main() -> int:
    if not RST.exists():
        print("build first: cd mboxfw && make", file=sys.stderr)
        return 2

    body = scan_main_body()
    if not body:
        print("could not locate _main body in main.rst", file=sys.stderr)
        return 2

    # Find all lcall / ljmp / conditional branch targets in main body.
    calls          = []    # (line_no, callee)
    conditionals   = []    # (line_no, mnemonic)
    RE_LCALL       = re.compile(r"\blcall\s+([_a-zA-Z0-9$]+)")
    RE_CJUMP       = re.compile(r"\b(jnz|jz|jc|jnc|jb|jnb|jbc|cjne|djnz)\b")

    for line_no, raw in body:
        m = RE_LCALL.search(raw)
        if m:
            calls.append((line_no, m.group(1)))
            continue
        if RE_CJUMP.search(raw):
            conditionals.append((line_no, raw.strip()))

    # Expected call order:
    #   _check_boot_dfu_button
    #   _usb_init
    #   _hw_init
    #   _cs8427_boot_init
    #   _codec_init
    # (buttons_poll and usb_service are inside the polling loop, but
    #  our scan stops at the sjmp back — they still show if inline)
    expected = [
        "_check_boot_dfu_button",
        "_usb_init",
        "_hw_init",
        "_cs8427_boot_init",
        "_codec_init",
    ]
    seen = [c for _, c in calls]

    missing = [e for e in expected if e not in seen]
    if missing:
        print(f"CONN REACH FAIL: main() body missing expected calls:",
              file=sys.stderr)
        for m in missing:
            print(f"  - {m}", file=sys.stderr)
        print("\nRestore the phase call or explain the removal.",
              file=sys.stderr)
        return 1

    # usb_init must be called exactly once — no bug where it's
    # inadvertently called twice (which could reset EP0 state
    # mid-enumeration).
    n_usb = seen.count("_usb_init")
    if n_usb != 1:
        print(f"CONN REACH FAIL: _usb_init called {n_usb} times in main"
              f" (expected exactly 1)", file=sys.stderr)
        return 1

    # check_boot_dfu_button must come BEFORE usb_init (#172).
    #
    # This is load-bearing for the check below, which looks for conditional
    # branches BETWEEN the two calls. With the escape placed after hw_init --
    # where it sat on 2026-08-03 -- that range runs backwards, is empty, and
    # the branch check passes without examining a single instruction. The gate
    # went vacuous the moment the call moved and said PASS the whole time.
    # Assert the order explicitly so the range cannot silently invert again.
    idx_usb   = seen.index("_usb_init")
    idx_hw    = seen.index("_hw_init")
    idx_btn   = seen.index("_check_boot_dfu_button")
    if idx_btn > idx_usb:
        print("CONN REACH FAIL: _check_boot_dfu_button is called AFTER",
              file=sys.stderr)
        print("_usb_init. The escape must run before anything that can hang,",
              file=sys.stderr)
        print("or a hang in usb_init/hw_init disables the only software",
              file=sys.stderr)
        print("recovery path -- BRICK_LOG.md #3, which cost an SDA short.",
              file=sys.stderr)
        print("It needs exactly two writes hoisted with it: P3 = 0xFF and",
              file=sys.stderr)
        print("GLOBCTL |= 0x02 (P3PUDIS). See #172.", file=sys.stderr)
        return 1
    if idx_usb > idx_hw:
        print("CONN REACH FAIL: _usb_init called AFTER _hw_init.",
              file=sys.stderr)
        print("Regression of task #47 never-brick guarantee — a hang in",
              file=sys.stderr)
        print("hw_init would now silently detach us from the bus.",
              file=sys.stderr)
        return 1

    # Between check_boot_dfu_button and usb_init, there must be NO
    # conditional jumps that could skip the usb_init call. (Between
    # main entry and check_boot_dfu_button is fine — that's just crt0
    # register-setup code.)
    idx_button_line = calls[idx_btn][0]
    idx_usb_line    = calls[idx_usb][0]
    dangerous = [(ln, m) for ln, m in conditionals
                 if idx_button_line < ln < idx_usb_line]
    if dangerous:
        print(f"CONN REACH FAIL: {len(dangerous)} conditional branch(es)",
              file=sys.stderr)
        print(f"between check_boot_dfu_button and usb_init — could skip",
              file=sys.stderr)
        print(f"usb_init on some path:", file=sys.stderr)
        for ln, m in dangerous:
            print(f"  main.rst:{ln}  {m}", file=sys.stderr)
        return 1

    # The escape reads P3, and both writes it depends on must precede it in
    # main() -- not merely exist in hw_init(), which now runs later. Without
    # P3PUDIS the internal pull-ups pin the port high and the read returns a
    # stuck 1 whatever is pressed, which is the state in which this escape
    # could never fire in any build up to 0x0015.
    # GLOBCTL is 0xFFB1 (movx via DPTR); P3 is direct SFR 0xB0.
    pre = [raw for ln, raw in body if ln < idx_button_line]
    pre_text = "\n".join(pre)
    if not re.search(r"mov\s+dptr,\s*#0xffb1", pre_text, re.I):
        print("CONN REACH FAIL: no GLOBCTL (0xFFB1) access before",
              file=sys.stderr)
        print("check_boot_dfu_button. P3PUDIS must be set first or the read",
              file=sys.stderr)
        print("is through the internal pull-ups and always reads high.",
              file=sys.stderr)
        return 1
    if not re.search(r"mov\s+_P3,\s*#0x[fF]{2}", pre_text):
        print("CONN REACH FAIL: no `P3 = 0xFF` before check_boot_dfu_button.",
              file=sys.stderr)
        print("The port latch must be high for the pins to read as inputs.",
              file=sys.stderr)
        return 1

    print(f"CONN REACH PASS: main() call order verified.")
    print(f"  P3 latch + P3PUDIS set before the DFU escape samples P3.")
    print(f"  order: {' → '.join(seen)}")
    print(f"  usb_init unconditionally reached before hw_init/cs8427/codec.")
    print(f"  USBCTL |= CONN (inside usb_init) executes on every non-DFU-reset boot.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
