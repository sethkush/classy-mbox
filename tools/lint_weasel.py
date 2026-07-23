#!/usr/bin/env python3
"""
Scan a commit message (or arbitrary text on stdin) for weasel words
that mask uncertainty. Every occurrence is flagged with a suggested
rewrite pattern.

Purpose: tonight's bricks lived exactly where I said "should work" or
"probably fine" instead of "verified by gate X". Words that mean
"I didn't actually check" are red flags in firmware work.

Usage:
    tools/lint_weasel.py < message.txt
    tools/lint_weasel.py --commit HEAD    # scan latest commit body

Exit codes:
    0 = clean (no weasel words)
    1 = weasel words found
    2 = usage error

Optional: hook into pre-commit alongside the citation gate.
"""

import re
import subprocess
import sys


# Pattern → hint. Match with \b word boundaries and case-insensitive.
WEASELS = [
    (r"\bshould(?:\s+(?:be|work|handle|help|prevent|not))?\b",
     "replace with 'verified by X' or 'X guarantees Y'"),
    (r"\bprobably\b",
     "quantify or remove"),
    (r"\blikely\b",
     "quantify or remove"),
    (r"\bseems?\b",
     "state a fact or don't state at all"),
    (r"\bappears?\s+to\b",
     "state directly what you observed"),
    (r"\bwe\s+think\b",
     "cite the evidence or drop"),
    (r"\bwe\s+believe\b",
     "cite the evidence or drop"),
    (r"\bhopefully\b",
     "hope is not a strategy; verify or note as unknown"),
    (r"\bmay(?:\s+not)?\s+(?:be|work|need|require)\b",
     "either 'does' / 'does not' or explicitly list conditions"),
    (r"\bmight(?:\s+not)?\s+(?:be|work|need|require)\b",
     "either 'does' / 'does not' or explicitly list conditions"),
    (r"\bkind\s+of\b",
     "be specific"),
    (r"\bmore\s+or\s+less\b",
     "give the actual numbers"),
    (r"\bfairly\b",
     "quantify"),
    (r"\bpretty\s+(?:sure|much|good|solid)\b",
     "state confidence explicitly (e.g. 'verified' or 'not yet tested')"),
    (r"\bI\s+guess\b",
     "state the evidence or explicit uncertainty"),
]


def scan(text: str) -> list[tuple[int, int, str, str, str]]:
    """
    Return a list of (line_no, col, matched_text, pattern_hint, line_text).
    """
    hits = []
    for i, line in enumerate(text.splitlines(), start=1):
        # Skip code blocks and quoted lines — those are often other
        # people's words, not our own claims.
        stripped = line.lstrip()
        if stripped.startswith(("|", ">", "```", "  #", "//", "/*", "*")):
            continue
        for pat, hint in WEASELS:
            for m in re.finditer(pat, line, re.IGNORECASE):
                hits.append((i, m.start() + 1, m.group(0), hint, line.rstrip()))
    return hits


def load_input(argv: list[str]) -> str:
    if not argv:
        return sys.stdin.read()
    if argv[0] == "--commit":
        ref = argv[1] if len(argv) > 1 else "HEAD"
        try:
            return subprocess.check_output(
                ["git", "log", "-1", "--format=%B", ref], text=True,
            )
        except subprocess.CalledProcessError:
            print(f"could not read commit {ref}", file=sys.stderr)
            sys.exit(2)
    if argv[0] == "--file":
        with open(argv[1]) as f:
            return f.read()
    print(f"usage: {sys.argv[0]} [--commit REF | --file PATH | < stdin]",
          file=sys.stderr)
    sys.exit(2)


def main() -> int:
    text = load_input(sys.argv[1:])
    hits = scan(text)
    if not hits:
        print("WEASEL PASS: no hedge words in input.")
        return 0

    print(f"WEASEL FAIL: {len(hits)} hedge word(s) found:", file=sys.stderr)
    for lineno, col, matched, hint, line in hits:
        print(f"  {lineno}:{col}  {matched!r}", file=sys.stderr)
        print(f"      → {hint}", file=sys.stderr)
        print(f"      | {line}", file=sys.stderr)
    print("\nReplace each with either a specific verification "
          "('verified by X gate') or an explicit 'not yet tested'.",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
