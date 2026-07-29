#!/usr/bin/env python3
"""
Verify quoted firmware bytes against the images they claim to come from.

Reverse-engineering notes are full of sentences like

    rev22 0x0F70 is 90 ff 68 f0 90 ff fc e0 44 c0, the middle of its RSTR
    handler

and that particular one was wrong: those bytes live at 0x0F79. The conclusion
drawn from it happened to be sound, so nothing downstream broke and nothing
caught it. That is the third citation of this kind to reach a commit in this
project -- see commits 2b09379 and d42730f, which removed two others.

The failure mode is specific and worth naming: the byte string is real and the
address is real, but they were never checked against each other. It survives
review because the surrounding argument is correct, and it survives every
existing gate because those check code, not prose.

This gate reads the claim back off the binary. For every span of text that
mentions an image and quotes a run of hex bytes, the bytes must actually appear
at one of the addresses named nearby in that image.

    python3 tools/check_byte_quotes.py

Exit 0 = every quote checks out, 1 = at least one does not.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware_stock")

IMAGES = {"rev20": os.path.join(FW, "rev20_firmware_code.bin"),
          "rev22": os.path.join(FW, "rev22_firmware_code.bin")}

# Where prose lives. Candidate sources are included because their comments carry
# most of the reverse-engineering argument now.
SCAN_DIRS = [
    ("firmware_stock/decomp", (".md", ".c", ".h")),
    ("firmware_stock/decomp/cand", (".c", ".h")),
    ("firmware_stock/decomp/cand/partial", (".c", ".md")),
    ("firmware_stock/disasm", (".md",)),
    ("tools", (".md",)),
]

# "rev20" / "rev 22" / "Rev 20". The image is what tells us which binary to read.
RE_IMAGE = re.compile(r"\brev\s*(20|22)\b", re.I)
RE_ADDR = re.compile(r"0x([0-9a-fA-F]{4})\b")
# Four bytes minimum. Three-byte runs are single 8051 instructions that recur
# all over an image ("90 ff 9b" appears four times in rev22 alone), so treating
# one as a positional claim produces noise rather than signal.
RE_BYTES = re.compile(r"\b((?:[0-9a-fA-F]{2}[ \t]+){3,}[0-9a-fA-F]{2})\b")

# A quote can be cited precisely in order to say it is NOT there -- "searching
# rev22 for `D0 83 D0 82` finds no occurrence". Those are claims about absence
# and must not be read as claims about position.
#
# Scoped to the quote's OWN line, deliberately. Matching anywhere in the window
# made the gate miss the very defect it was built for: the sentence above the
# bad citation said a *different* byte string occurs "zero times in rev22", and
# that phrase then excused the bad quote on the next line. Caught by the
# mutation test, which is the only reason this comment exists.
RE_NEGATIVE = re.compile(
    r"\b(zero times|no occurrence|finds no|does not|doesn't|never|nowhere|"
    r"no such|not present|absent|has no)\b", re.I)

# Runs that are obviously not a quote from an image.
SKIP_LINE = ("sfr-names-ok", "byte-quotes-ok")

# How many lines around a quote to search for the address and image it belongs
# to. Comment blocks wrap, so the address is often a line or two above.
WINDOW = 3


def load(path):
    return open(path, "rb").read() if os.path.exists(path) else None


def files_to_scan():
    seen, out = set(), []
    for rel, exts in SCAN_DIRS:
        d = os.path.join(ROOT, rel)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            p = os.path.join(d, fn)
            if os.path.isfile(p) and fn.endswith(exts) and p not in seen:
                seen.add(p)
                out.append(p)
    return out


def main():
    imgs = {k: load(v) for k, v in IMAGES.items()}
    missing = [k for k, v in imgs.items() if v is None]
    if missing:
        sys.exit(f"missing image(s): {', '.join(missing)}")

    bad, checked = [], 0
    for path in files_to_scan():
        rel = os.path.relpath(path, ROOT)
        lines = open(path, errors="replace").read().split("\n")
        for n, line in enumerate(lines):
            if any(s in line for s in SKIP_LINE):
                continue
            for m in RE_BYTES.finditer(line):
                raw = m.group(1)
                try:
                    quoted = bytes.fromhex(raw.replace("\t", " "))
                except ValueError:
                    continue
                lo = max(0, n - WINDOW)
                window = "\n".join(lines[lo:n + WINDOW + 1])
                revs = {f"rev{r}" for r in RE_IMAGE.findall(window)}
                if not revs:
                    continue          # no image named: not a claim we can check
                if RE_NEGATIVE.search(line):
                    continue          # a claim of absence, not of position
                # Addresses named in the window are the candidate anchors. The
                # quote has to sit at one of them, in one of the named images.
                addrs = {int(a, 16) for a in RE_ADDR.findall(window)}
                if not addrs:
                    continue
                checked += 1
                ok = any(imgs[r][a:a + len(quoted)] == quoted
                         for r in revs for a in addrs)
                if ok:
                    continue
                # A table is often quoted as successive records after one base
                # address -- "?C_INITSEG at 0x0F9C is 01 22 00 | 01 20 00 |
                # ..." -- where only the first record sits at the cited
                # address, and the listing wraps over several lines. Treat a
                # maximal block of consecutive lines carrying byte runs as one
                # quoted sequence, and accept every run in it if the whole
                # sequence lands at a cited address.
                #
                # This deliberately does NOT accept a lone quote found at some
                # unrelated offset near a cited address, which is exactly the
                # rev22 0x0F70-vs-0x0F79 defect this gate exists to catch: a
                # single-line quote has no block to hide in.
                i = n
                while i > 0 and RE_BYTES.search(lines[i - 1]):
                    i -= 1
                j = n
                while j + 1 < len(lines) and RE_BYTES.search(lines[j + 1]):
                    j += 1
                if j > i:
                    joined = b"".join(
                        bytes.fromhex(x.replace("\t", " "))
                        for k in range(i, j + 1)
                        for x in RE_BYTES.findall(lines[k]))
                    # The base address introduces the table, so look above it.
                    head = "\n".join(lines[max(0, i - WINDOW - 3):j + 1])
                    base = {int(a, 16) for a in RE_ADDR.findall(head)}
                    brev = {f"rev{r}" for r in RE_IMAGE.findall(head)} or revs
                    if any(imgs[r][a:a + len(joined)] == joined
                           for r in brev for a in base):
                        continue
                # Say where it *does* live -- that is the whole repair.
                found = []
                for r in revs:
                    i = imgs[r].find(quoted)
                    while i != -1 and len(found) < 4:
                        found.append(f"{r} 0x{i:04X}")
                        i = imgs[r].find(quoted, i + 1)
                bad.append((rel, n + 1, raw.strip(), sorted(revs),
                            sorted(addrs), found))

    if bad:
        print(f"BYTE-QUOTE FAIL: {len(bad)} quote(s) not at the cited address\n")
        for rel, n, raw, revs, addrs, found in bad:
            print(f"  {rel}:{n}")
            print(f"      quoted : {raw}")
            print(f"      images : {', '.join(revs)}")
            print(f"      cited  : {', '.join(f'0x{a:04X}' for a in addrs)}")
            print(f"      actually at: {', '.join(found) if found else
                                        'nowhere in the cited image(s)'}")
        print("\nA byte string next to an address is a claim that the two match.")
        print("Fix the address, fix the bytes, or drop the quote. To suppress a")
        print("line that is deliberately quoting something else, add the marker")
        print("'byte-quotes-ok'.")
        return 1

    print(f"BYTE-QUOTE PASS: {checked} quoted byte run(s) verified against "
          f"{len(imgs)} image(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
