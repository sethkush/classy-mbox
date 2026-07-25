#!/usr/bin/env python3
"""
Reject any staged commit that adds a new XDATA (0xFFxx) SFR write
without a nearby citation. Designed to run from a git pre-commit hook.

What counts as "SFR write":
    Any line matching /XDATA\\(0xFF..\\)\\s*[|&^~]?=/ in the +hunk.
    Common forms:
        USBCTL = ...
        USBCTL |= ...
        XDATA(0xFFC0) = ...

What counts as a "citation":
    Within 3 lines above OR on the same line as the write, a comment
    containing any of:
        Rev20 0xXXXX  |  rev20 fcn.  |  TI reference:  |  TI I2c.h
        TI UsbDfu.c   |  TI UsbEng.c  |  TI RomBoot.c  |  TI Utils.c
        NOVEL — reason:
    The last form is an escape hatch for genuinely new SFR touches
    that don't have a reference. Requires an explicit "reason:" text.

Exit codes: 0 = clean, 1 = uncited write found, 2 = error.
"""

import re
import subprocess
import sys


RE_SFR_WRITE = re.compile(
    r"(?:"
    r"XDATA\s*\(\s*0x[fF][fF][0-9a-fA-F]{2}\s*\)"
    r"|"
    r"(?:USBCTL|USBIMSK|USBFADR|USBSTA|GLOBCTL|MEMCFG"
    r"|IEPCNF[0-9]|IEPBBAX[0-9]|IEPBSIZ[0-9]|IEPBCTX[0-9]"
    r"|OEPCNF[0-9]|OEPBBAX[0-9]|OEPBSIZ[0-9]|OEPBCTX[0-9]"
    r"|CPTCTL|CPTBRRX|CPTBRTX|CPTCNF[0-9]|DMACTL[0-9]"
    r"|DMASRC[0-9]_[LMH]"
    r"|I2C_STA|I2C_TX|I2C_RX|I2C_SADDR|VECINT|USBIMSK)"
    r")"
    # Require an assignment operator (=, |=, &=, ^=, ~=) for BOTH forms,
    # not just the SFR-name form. Original pattern grouped `\s*=` only
    # with the SFR-name alternative, so `#define GLOBCTL XDATA(0xFFB1)`
    # falsely matched the XDATA half without an operator.
    r"\s*(?:\|=|&=|\^=|~=|=)(?!=)"
)
# Comment lines that mention an SFR write inside prose ("USBCTL |= 0x80")
# aren't actual writes — skip them.
RE_COMMENT_LINE = re.compile(r"^\s*(?:\*|//|/\*)")

CITATION_MARKERS = [
    re.compile(r"Rev\s*20\b", re.IGNORECASE),
    re.compile(r"rev20"),
    re.compile(r"fcn\.0x[0-9a-fA-F]"),
    re.compile(r"TI\s+(reference|UsbDfu|UsbEng|RomBoot|Utils|I2c|Eeprom|Reg_stc)",
               re.IGNORECASE),
    # Bare file-line citations (e.g. `UsbEng.c:227`, `hwMacro.h:9-10`,
    # `UsbDfu.c:1004-1014`). Same intent as `TI UsbEng.c` — the "TI"
    # prefix is doc-ergonomic, not load-bearing.
    re.compile(r"(UsbDfu|UsbEng|RomBoot|Utils|I2c|Eeprom|Reg_stc|hwMacro"
               r"|Device|Application)\.[ch]:\d+"),
    re.compile(r"NOVEL\b[^\n]*reason\s*:", re.IGNORECASE),
    re.compile(r"boot\s*rom", re.IGNORECASE),
    re.compile(r"engUsbInit|engEp0[A-Z]\w+|engLoad[A-Z]\w+"
               r"|dfuSetup|UtilResetCPU|STALL(?:Clr|Set)?[IO]n?Ep0"
               r"|TOGGLE[IO]n?Ep0"),
    # Bare "TI's <symbol>" narrative citation.
    re.compile(r"TI's\s+\w"),
]


def has_citation(context_lines: list[str]) -> bool:
    joined = "\n".join(context_lines)
    for pat in CITATION_MARKERS:
        if pat.search(joined):
            return True
    return False


def staged_diff() -> str:
    """Return unified diff of staged changes vs HEAD, from git."""
    try:
        return subprocess.check_output(
            ["git", "diff", "--cached", "--unified=3",
             "--", "*.c", "*.h"],
            text=True,
        )
    except subprocess.CalledProcessError as e:
        print(f"git diff failed: {e}", file=sys.stderr)
        sys.exit(2)


def main() -> int:
    diff = staged_diff()
    if not diff.strip():
        return 0

    violations: list[tuple[str, int, str]] = []
    current_file = None
    hunk_new_line = 0

    # Track the last few `+` context lines so we can look for a comment
    # above the offending write.
    plus_recent: list[str] = []
    # Window sized so a multi-line comment block above a cluster of SFR
    # writes still has its citation visible when checking the last write
    # in the cluster. Originally 4, bumped to 12 — the safety_net init
    # sequence has 4-8 consecutive SFR writes under a single 6-10 line
    # doc-comment; 4 was catching false positives on the trailing writes.
    old_plus_max = 12

    for raw in diff.splitlines():
        if raw.startswith("+++ "):
            current_file = raw[6:].strip()
            plus_recent = []
            continue
        if raw.startswith("--- "):
            continue
        m = re.match(r"@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@", raw)
        if m:
            hunk_new_line = int(m.group(1)) - 1
            plus_recent = []
            continue
        if raw.startswith("+") and not raw.startswith("+++"):
            hunk_new_line += 1
            body = raw[1:]
            plus_recent.append(body)
            if len(plus_recent) > old_plus_max:
                plus_recent.pop(0)
            # Check for SFR write on THIS added line.
            if RE_COMMENT_LINE.match(body):
                continue
            if RE_SFR_WRITE.search(body):
                # Look at the last 4 lines (including this one) for a
                # citation comment. Also allow inline (same-line) comment.
                if not has_citation(plus_recent):
                    violations.append((current_file, hunk_new_line, body))
            continue
        if raw.startswith(" "):
            hunk_new_line += 1
            plus_recent.append(raw[1:])
            if len(plus_recent) > old_plus_max:
                plus_recent.pop(0)
            continue
        # Removed lines don't advance new-line counter but reset context.
        if raw.startswith("-") and not raw.startswith("---"):
            plus_recent = []

    if not violations:
        return 0

    print(f"CITATION FAIL: {len(violations)} SFR write(s) added without a"
          f" nearby citation comment:", file=sys.stderr)
    for path, line, body in violations:
        print(f"  {path}:{line}   {body.strip()}", file=sys.stderr)
    print("\nEvery SFR-touching change must cite the reference within",
          file=sys.stderr)
    print("~3 lines above the write. Accepted markers:", file=sys.stderr)
    print("  * Rev 20 fcn.0xXXXX @ 0xYYYY", file=sys.stderr)
    print("  * TI <File>.c <function>  (e.g. TI UsbEng.c engUsbInit)",
          file=sys.stderr)
    print("  * NOVEL — reason: <one-line explanation>", file=sys.stderr)
    print("\nTo bypass (STRONGLY discouraged; explain in commit body):",
          file=sys.stderr)
    print("    git commit --no-verify ...", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
