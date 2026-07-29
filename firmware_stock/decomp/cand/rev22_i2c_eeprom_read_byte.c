// MATCH: image=rev22 addr=0x0D11 len=71 func=i2c_eeprom_read_byte cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Read one byte from the serial EEPROM over the TAS1020B I2C master, Rev 22.
 *
 *   unsigned char i2c_eeprom_read_byte(unsigned char addr_hi,  // Keil: R7
 *                                      unsigned char addr_lo); // Keil: R5
 *   returns the byte in R7.
 *
 * REV 20 -> REV 22 DELTA: ONE DEAD INSTRUCTION REMOVED, no behavioural change.
 * The two bodies are byte-identical for the first 69 bytes; then
 *     rev20 0x0D21   MOVX A,@DPTR / MOV R6,A / MOV R7,A / RET   (72 B total)
 *     rev22 0x0D55   MOVX A,@DPTR /            MOV R7,A / RET   (71 B total)
 * i.e. Rev 20 kept a `MOV R6,A` (opcode 0xFE) whose value is never read again
 * -- the trace of a local variable that was assigned and then returned.  Rev 22
 * dropped it.  Verified by comparing the two byte ranges directly:
 *   rev20 0x0CDD..0x0D24 and rev22 0x0D11..0x0D57 differ only by that byte.
 * Its sibling i2c_eeprom_write3 is byte-identical between the images
 * (rev20 0x0BEE, rev22 0x0BDA), so this is the only I2C change in Rev 22.
 * Nothing about EEPROM timing, error handling or protocol changed.
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY -- same register-parameter class as
 * i2c_eeprom_write3 and ep0_buf_clear_byte.  Keil takes the two arguments in
 * R7/R5 and returns in R7; SDCC uses DPL/DPH and returns in DPL.
 *
 * Rev 22 caller evidence for the argument order: cmd11_eeprom_selftest
 * (rev22 0x04C8) loads R5=0xFF, R7=0x1F at 0x04E2-0x04E4 and calls here,
 * reading EEPROM address 0x1FFF; it complements the result, writes it back
 * with i2c_eeprom_write3(0x1F, 0xFF, ~old) at 0x04F2, reloads R5=0xFF (the
 * write's delay loop destroys R5) and re-reads at 0x04F7.  The CJNE at
 * rev22 0x04FE falls through to `CLR 0x16` when the readback equals the
 * complemented value, so bit 0x16 (IRAM 0x22.6) is cleared on a PASS -- the
 * same encoding as rev20 0x04FA/0x04FD.  That bit is not private to the
 * self-test; it is bit 6 of the front-panel chain-A latch byte, and
 * cand/shiftreg8_commit.c owns the full account.  Note the test is
 * destructive and does not restore: it leaves byte 0x1FFF complemented.
 *
 * Registers (reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h):
 *   0xFFC0  I2CSTA / I2CCTL   status when read, control when written
 *   0xFFC1  I2CDATO           transmit data / sub-address bytes
 *   0xFFC2  I2CDATI           receive data
 *   0xFFC3  I2CADR            slave address; writing it issues a START
 * I2CSTA bits, from reference/.../v1.8/ROM/i2c.h:
 *   0x01 STOP_WRITE  0x02 STOP_READ  0x08 XMIT_DATA_EMPTY  0x20 ERROR
 *   0x80 RCV_DATA_FULL
 * `JNB 0xE3` and `JNB 0xE7` are ACC.3 and ACC.7 of the value just read -- 0xE0
 * is ACC and it is bit-addressable -- not bits in IRAM.
 *
 * Protocol: the standard 24Cxx random read.  Write phase with slave 0xA0 sets
 * the 16-bit sub-address; then the slave address is re-issued with bit 0 set
 * (0xA1) for a repeated START in read direction, a dummy byte is pushed into
 * I2CDATO to clock the read out, STOP_READ is armed so the controller releases
 * the bus after this one byte, and the result is taken from I2CDATI.
 *
 * `ORL 0x06,#0x01` is a byte operation on direct address 0x06, which in
 * register bank 0 is R6 -- so it is `slave_addr |= 1`, turning 0xA0 into 0xA1.
 * It is not a bit operation: `ORL bit` does not exist, and bit 0x06 would be
 * IRAM 0x20.6.  Keil kept the slave address in R6 precisely so it could patch
 * the direction bit in place.  (This is also why dropping the trailing
 * `MOV R6,A` is harmless: R6's last use is at 0x0D3D.)
 *
 * Ordering note: STOP_READ is set AFTER the dummy I2CDATO write (0xFFC1 <- 0,
 * then 0xFFC0 |= 0x02), which is the reverse of TI's single-byte read path in
 * ROM/I2c.c, which does `I2CSTA |= STOP_READ; I2CDATO = 0xFF;`.  Recorded as an
 * observed difference; the stock order demonstrably works on this hardware, but
 * why the controller tolerates it has not been established from the datasheet,
 * so no mechanism is asserted here.  Rev 22 kept the same order.
 *
 * No wait loop tests ERROR (0x20) and none has a timeout, unlike TI's
 * WaitOnI2C: an absent or NAKing EEPROM hangs the routine.
 */
void i2c_eeprom_read_byte(void) __naked {
    __asm
        mov   dptr,#0xffc0        ; I2CSTA
        movx  a,@dptr
        anl   a,#0xfc             ; clear STOP_WRITE|STOP_READ, keep the rest
        movx  @dptr,a

        mov   r6,#0xa0            ; EEPROM slave address, write direction
        mov   dptr,#0xffc3        ; I2CADR
        mov   a,r6
        movx  @dptr,a             ; START + slave address

        mov   dptr,#0xffc1        ; I2CDATO
        mov   a,r7                ; sub-address, high byte
        movx  @dptr,a
    0001$:
        mov   dptr,#0xffc0
        movx  a,@dptr
        jnb   acc.3,0001$         ; spin until XMIT_DATA_EMPTY (no timeout)

        mov   dptr,#0xffc1        ; I2CDATO
        mov   a,r5                ; sub-address, low byte
        movx  @dptr,a
    0002$:
        mov   dptr,#0xffc0
        movx  a,@dptr
        jnb   acc.3,0002$

        orl   0x06,#0x01          ; R6 |= 1 -> 0xA1, read direction

        mov   dptr,#0xffc3        ; I2CADR
        mov   a,r6
        movx  @dptr,a             ; repeated START + 0xA1

        mov   dptr,#0xffc1        ; I2CDATO
        clr   a
        movx  @dptr,a             ; dummy byte: clocks the read out

        mov   dptr,#0xffc0        ; I2CCTL
        movx  a,@dptr
        orl   a,#0x02             ; STOP_READ: release the bus after one byte
        movx  @dptr,a
    0003$:
        mov   dptr,#0xffc0
        movx  a,@dptr
        jnb   acc.7,0003$         ; spin until RCV_DATA_FULL

        mov   dptr,#0xffc2        ; I2CDATI
        movx  a,@dptr
        mov   r7,a                ; Keil returns a char in R7
        ret                       ; rev20 0x0D23 had a dead MOV R6,A here
    __endasm;
}
