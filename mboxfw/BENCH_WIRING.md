
## Both units moved behind the Genesys hub — 2026-08-15

| unit | serial | port path |
|---|---|---|
| B | `RK1672500M` | **2-1.3.1** |
| A | `RK10874600Q` | **2-1.3.2** |

Previously 2-1.2 and 2-1.4 directly on the Intel hub. Serials are compiled in
(`MBOX_UNIT=A`/`=B`), so what matters is which unit is which, not which port —
but every `--addr`/port-path recipe written before this date names the old paths.

**The Genesys hub does NOT cut VBUS either.** The 2026-08-11 entry above closed
this for the Intel `8087:0024`; the Genesys `05e3:0605` was only ever tested on
an EMPTY port, which could not tell "power removed" from "nothing there". With a
unit on it: `-a off`, 20 s, `-a on` left the device node present throughout and
brought the unit back with counters CLIMBING — bus resets 3→6, setup 69→114,
iep0 114→212. A cold boot drives those down.

So **both hubs on this host report `ppps`, both accept the request, both report
`0000 off`, and neither removes power.** The status bit reflects the request, not
the port. There is no remote power cycle on this bench by any route now tested,
and the question should not be re-opened without new hardware — a hub with a
genuinely switched VBUS rail, verified by a counter going DOWN.
