// MATCH: image=rev20 addr=0x0FF4 len=13 func=codec_port_cfg3_commit cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Write one value to both codec-port frame-config registers and re-enable the
 * codec port.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY, and for a reason that is worth recording:
 * this is not a source-level function at all. It is entered with BOTH DPTR and
 * A already live -- the first instruction stores A through a DPTR the caller
 * set up -- which no C calling convention on C51 produces. Keil passes the
 * first char parameter in R7, never in A, and never expects a caller to
 * pre-load DPTR.
 *
 * Call sites here are cited by the FIRST instruction of the caller's prologue,
 * not by the LCALL. Both rev20 callers (0x034A and 0x0355) do the same
 * two-instruction prologue:
 *
 *      MOV DPTR,#0xFFDE      ; CPTCNF3
 *      MOV A,#0xAC / #0xA8
 *      LCALL 0x0FF4
 *
 * so the factored block starts one instruction *after* the DPTR load. Rev 22
 * factors the same code one instruction earlier: its block at 0x0FE2 opens with
 * `MOV DPTR,#0xFFDE` and its callers (0x0356, 0x035E) only load A -- same
 * anchoring rule, so those are the `MOV A,#imm`, with the LCALL two bytes
 * later at 0x0358 / 0x0360. A source
 * function boundary would not move like that between revisions; a compiler
 * looking for the longest common instruction suffix would, because the code
 * around the call sites changed. Read this as Keil's common-block
 * subroutine-extraction pass (OPTIMIZE level 9), not as something someone
 * wrote. That inference is the best explanation of the two encodings; it is not
 * something the images state outright.
 *
 * What it does, in TAS1020B terms:
 *   CPTCNF3   (0xFFDE) = A   codec port frame config 3 -- transmit side
 *   CPTRXCNF3 (0xFFD5) = A   the receive-side mirror of the same field layout
 *   GLOBCTL   (0xFFB1) |= 0x01   set CPTEN, re-enabling the codec port
 *
 * The GLOBCTL read-modify-write is the ordering that matters: CPTCNF/CPTRXCNF
 * are only writable while CPTEN is clear, so every caller drops CPTEN, pokes
 * the config, and comes here to bring the port back. hw_master_init (0x08CB)
 * follows the same rule at cold start, setting CPTEN last of all.
 *
 * Values seen at the two rev20 call sites: 0xAC and 0xA8, differing in bit 2.
 * The same pair appears at the rev22 sites, so whatever bit 2 selects is
 * unchanged across revisions.
 */
void codec_port_cfg3_commit(void) __naked {
    __asm
        movx  @dptr,a              ; CPTCNF3 <- A   (DPTR preloaded by caller)
        mov   dptr,#0xffd5         ; CPTRXCNF3
        movx  @dptr,a              ; same value to the receive-side config
        mov   dptr,#0xffb1         ; GLOBCTL
        movx  a,@dptr              ; read-modify-write: the other GLOBCTL bits
        orl   a,#0x01              ;   (12 MHz select, LPWR) must survive
        movx  @dptr,a              ; CPTEN = 1: codec port runs again
        ret
    __endasm;
}
