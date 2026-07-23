# Mutation-test coverage gaps

Run `tools/mutation_test.sh` to reproduce. Each row below is a
mutation whose intended gate did NOT catch it. These represent real
false-negatives in the current gate suite.

## Uncaught mutations (2026-07-23)

### 1. `USBCTL |= USBCTL_CONN` removed — sim_smoke still passed

**Why it slipped:** sim_smoke reads XDATA[0xFFFC] post-run and checks
bit 7 is set. With the CONN write removed, the sim's default initial
state for that address happens to have bit 7 set (0xA0) so the check
still passes.

**Fix (future):** sim_smoke should verify USBCTL was ACTIVELY written
by our code, not just that its final value happens to look right. Two
options:
  (a) Set XDATA[0xFFFC] = 0 explicitly before `run` starts, then check
      bit 7 is set post-run.
  (b) Rely on the SFR audit (`audit_sfr_writes.py`) to catch the
      removed write — a manifest diff surfaces it as `- 0xfffc runtime -`.

Audit DOES catch this mutation independently; sim_smoke has redundant
coverage but with a false-negative on this specific mutation shape.

### 2. `reply_zero_length` removed from SET_ADDRESS handler only

**Why it slipped:** `verify_setup_paths.py` checks that
`reply_zero_length()` is REACHABLE somewhere in the compiled image.
It IS still reachable — from SET_CONFIG and SET_INTERFACE handlers.
The gate confirms the symbol exists but not that every SET request
that needs an ACK actually calls it.

**Fix (future):** would need per-branch static analysis of
`handle_setup` — for every switch case, verify the reachable
control flow reaches a `reply_zero_length` OR a `stage_reply` OR a
`stall` call.

### 3. `MBOX_VID` changed away from 0x0DBA

**Test error:** the mutation `sed` targeted `mboxfw/src/usb.c` but
the VID macro is defined in `mboxfw/include/usb.h`. Mutation applied
to a file that doesn't affect the descriptor. Fix: target the header.

Once targeted correctly, `verify_descriptors.py` will still fail
because it doesn't check specific VID/PID values, only structural
descriptor validity. Would need a gate that pins VID=0x0DBA.

### 4. `wrap_hex.py` pad-to-8192 reintroduced

**Test error:** the sed regex escaping was wrong; mutation didn't
apply. Fix: use single-quoted sed with different delimiter to avoid
the double-escape mess.

## Caught mutations (working as intended)

- `USBCTL = 0xC0` assignment (flash #2 replay) → SFR audit catches
- Removed `check_boot_dfu_button` call → `verify_setup_paths` catches
- Removed I²C dummy `I2C_TX = 0xFF` in eeprom_read → SFR audit catches

## Value of this file

The gap between "gates report PASS" and "firmware is definitely
correct" is exactly the mutations in this file. When adding a new
gate or tightening an existing one, come here first — pick a
currently-uncaught mutation and make sure the new gate catches it.
