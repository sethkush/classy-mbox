#!/usr/bin/env python3
"""Every preflight gate must run per-commit, or be excluded on purpose.

WHY THIS EXISTS
---------------
`ci_bisect_gates.sh` exists so that when a flash bricks a unit, `git bisect`
can find the commit that did it. That only works if every commit in the range
was actually checked. A gate absent from the per-commit runner is a regression
bisect structurally cannot find.

On 2026-08-05 (#158) it ran SIX gates out of about thirty, while its own
comment claimed "every non-hardware gate we have". The excuse did not survive
contact: `sim_smoke.sh` drives s51 too, so "hardware-dependent" never
distinguished the omitted ones. Speed did — and then not even that, because
the eleven omitted static gates cost **0.6 seconds in total**. Among the
missing: diff_vs_rev20, audit_sfr_writes, the citation gates, and
sim_p1_waveform, which is the only check of CS8427 SPI framing anywhere.

Two hand-maintained lists in two files drift. This makes the drift an error.

WHAT IT CHECKS
--------------
Every `tools/<gate>` invoked by preflight.sh's mboxfw path either appears in
ci_bisect_gates.sh, or is listed in EXCLUDED below with a reason. Adding a
gate to preflight and forgetting the runner now fails.

EXCLUSIONS ARE NOT A LOOPHOLE. Each needs a reason that survives being read
aloud. "It is slow" is not one on its own — sim_p1_waveform is 54 s and is in,
behind an explicit CI_SKIP_SIM opt-out, because the alternative is shipping
unverified SPI framing.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PREFLIGHT = REPO / "tools" / "preflight.sh"
CI = REPO / "tools" / "ci_bisect_gates.sh"

# gate -> why it is not in the per-commit runner
EXCLUDED = {
    # Needs a built IMAGE PATH argument and a target choice; preflight is
    # given one, the bisect loop is not. The gates it wraps are all here
    # individually.
    "preflight.sh": "the runner itself",
    # Requires the mboxflash binary and an image argument.
    "e2e_flash_loop.sh": "drives a real flash cycle; hardware, not CI",
    "dfu_timing_profile.sh": "measures DFU response timing against hardware",
    # safety_net target only; ci_bisect builds safety_net but the gate needs
    # its image path. Covered at preflight time for that target.
    "verify_safety_net.py": "safety_net image path argument; runs in preflight's safety_net branch",
    "diff_vs_rev20_safety_net.py": "safety_net target only; needs safety_net/build present",
    # Library/entry points invoked BY other gates rather than standalone
    # checks with their own pass/fail.
    "wrap_hex.py": "build step, not a gate; its regression is test_wrap_hex_golden.py",
    "xdata_access_map.py": "in the runner as --selftest",
    "mboxtlm.py": "in the runner as --selftest",
    # Decompilation gates: they check firmware_stock/decomp against the stock
    # images, which no mboxfw commit can change. Running them per mboxfw
    # commit re-proves an invariant unrelated to the range being bisected.
    "match51.py": "checks decomp vs stock images; independent of mboxfw commits",
    "link51.py": "checks decomp vs stock images; independent of mboxfw commits",
    "check_sdcc_version.sh": "pins the host toolchain; identical for every commit in a run",
}


def gates_in(path: Path) -> set[str]:
    """Gate filenames referenced by a runner script.

    Matches literal `tools/<name>.py` AND bare stems, because
    ci_bisect_gates.sh invokes most of its gates from `for g in a b c; do
    python3 "tools/$g.py"` loops. The first version of this guard matched only
    the literal form and reported fifteen gates as missing that were sitting
    right there in a loop -- a checker that does not model how the thing it
    checks actually works.
    """
    text = path.read_text(errors="ignore")
    found = set(re.findall(r"tools/([A-Za-z0-9_]+\.(?:py|sh))", text))
    # bare stems, as they appear in the runner's for-loops
    stems = set(re.findall(r"\b([a-z0-9_]{4,})\b", text))
    for stem in stems:
        for ext in (".py", ".sh"):
            if (REPO / "tools" / (stem + ext)).exists():
                found.add(stem + ext)
    return found


def main() -> int:
    if not PREFLIGHT.exists() or not CI.exists():
        print("GATE-COVERAGE FAIL: preflight.sh or ci_bisect_gates.sh missing")
        return 1

    pre = gates_in(PREFLIGHT)
    ci = gates_in(CI)

    missing = sorted(g for g in pre - ci if g not in EXCLUDED)
    stale = sorted(g for g in EXCLUDED if g not in pre and g != "preflight.sh")

    rc = 0
    if missing:
        print("GATE-COVERAGE FAIL: %d preflight gate(s) never run per-commit "
              "and are not excluded:\n" % len(missing))
        for g in missing:
            print("    %s" % g)
        print("\nci_bisect_gates.sh is what makes `git bisect` usable after a")
        print("brick. A gate missing from it is a regression bisect cannot")
        print("find. Add it to the runner, or add it to EXCLUDED in")
        print("tools/check_gate_coverage.py with a reason.")
        rc = 1

    if stale:
        print("\nGATE-COVERAGE FAIL: %d exclusion(s) name a gate preflight no "
              "longer runs:" % len(stale))
        for g in stale:
            print("    %s  (%s)" % (g, EXCLUDED[g]))
        print("\nDrop the stale entry — an exclusion for a gate that does not")
        print("exist hides the next real one.")
        rc = 1

    if rc == 0:
        print("GATE-COVERAGE PASS: %d preflight gate(s), %d run per-commit, "
              "%d excluded with reasons"
              % (len(pre), len(pre & ci), len(pre & set(EXCLUDED))))
    return rc


if __name__ == "__main__":
    sys.exit(main())
