# Declared partial matches

Candidates here reproduce their stock function except for a stated number of
bytes, and the reason is a capability the compiler does not have rather than
something still to be figured out. Each declares `partial=N` in its `MATCH:`
header and argues the N in a comment at the site.

They are kept out of `cand/` proper for two reasons. The gate over `cand/*.c`
demands an exact 100%, and that is only worth something if nothing in it is
allowed to be approximate. And `link51.py` places candidates at their stock
addresses, which a function three bytes too long cannot be: it would run into
its neighbour.

`tools/match51.py` still checks these, and checks the shortfall is *exactly*
the declared N -- so a partial cannot quietly grow, and one that someone later
closes shows up as a failure telling you to move it back into `cand/`.

The recurring cause so far is Keil's inter-procedural register analysis. Keil
knew which registers a callee left alone and kept values live across calls;
SDCC reloads. See `std_get_interface`, where DPTR survives a call into a
helper that only writes IRAM.
