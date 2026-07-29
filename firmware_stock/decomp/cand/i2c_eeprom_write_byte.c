// MATCH: image=rev20 addr=0x0BEE len=87 func=i2c_eeprom_write_byte cflags=--peep-file,firmware_stock/decomp/keil.peep
/* Write one byte to the serial EEPROM over the TAS1020B I2C master.
 *
 *   void i2c_eeprom_write_byte(unsigned char addr_hi,   // Keil: R7
 *                              unsigned char addr_lo,   // Keil: R5
 *                              unsigned char val);      // Keil: R3
 *
 * WRITTEN AS ASSEMBLY DELIBERATELY -- the register-parameter class already
 * covered by ep0_buf_clear_byte and code_read_byte_at_srcptr. Keil C51 passes
 * the first three char arguments in R7/R5/R3; SDCC passes the first in DPL and
 * the rest in DPH/B. No arrangement of C reaches this prologue, and the body
 * then keeps those same registers live alongside the R4:R5 delay counter.
 * Callers confirm the order: evt0d_invalidate_boot_eeprom (rev20 0x0518; the
 * same code is cmd13_invalidate_boot_eeprom at rev22 0x0517) does
 * CLR A / MOV R3,A / MOV R5,A / MOV R7,A and calls here to zero EEPROM byte
 * 0x0000, and cmd11_eeprom_selftest (rev20 0x04DE-0x04EE, rev22 0x04E2-0x04F2)
 * loads R7=0x1F, R5=0xFF for address 0x1FFF.
 *
 * Registers (reference/tas1020a/ti_uac_reference/ROM/Reg_stc1.h):
 *   0xFFC0  I2CSTA / I2CCTL   status when read, control when written
 *   0xFFC1  I2CDATO           transmit data / sub-address bytes
 *   0xFFC3  I2CADR            slave address; writing it issues START
 * I2CSTA bit meanings are from reference/.../v1.8/ROM/i2c.h:
 *   0x01 STOP_WRITE   0x02 STOP_READ   0x08 XMIT_DATA_EMPTY   0x20 ERROR
 * `JNB 0xE3` is ACC.3 (XMIT_DATA_EMPTY) on the value just read, not a bit in
 * IRAM -- 0xE0 is ACC and it is bit-addressable.
 *
 * Device: slave 0xA0 with a 16-bit sub-address, i.e. a 24Cxx of at least 8 KiB
 * (the 0x1FFF access above needs 8 KiB). This is the same EEPROM the boot ROM
 * reads its signature and payload from.
 *
 * TIMING -- this is the part that matters to the flasher:
 *
 *   * There is no ACK polling anywhere. After the data byte is handed to
 *     I2CDATO with STOP_WRITE armed, the routine waits for XMIT_DATA_EMPTY and
 *     then burns a fixed 16-bit countdown from 0xFFFF. That countdown is the
 *     write-cycle hold (tWR) for the EEPROM's internal program cycle. The
 *     counter is reloaded on every pass of the wait loop, so the full 65535
 *     iterations always run: the flag cannot already be set on the first read,
 *     the byte having only just been queued.
 *   * The 0x00FF countdown near the top runs after I2CADR is written -- that
 *     is, while START plus the slave address are on the wire -- and before the
 *     first sub-address byte. Its apparent purpose is to give a device still
 *     finishing a previous program cycle time to start acknowledging.
 *   * Neither wait loop tests ERROR (0x20) and neither has a timeout, unlike
 *     TI's WaitOnI2C. If the EEPROM is absent or NAKs, both JNB spins hang
 *     forever. Firmware that talks to the EEPROM therefore assumes it is
 *     present and healthy.
 *
 * Rev 22 has this function verbatim at 0x0BDA: all 87 bytes are byte-identical
 * to rev20 0x0BEE (checked against both images). Only the read side differs.
 *
 * R6 exists only to preserve addr_lo, because the first delay loop destroys
 * the R4:R5 pair that addr_lo arrived in.
 */
void i2c_eeprom_write_byte(void) __naked {
    __asm
        mov   r6,0x05             ; save addr_lo (R5) -- the delay loop eats R5

        mov   dptr,#0xffc0        ; I2CSTA
        movx  a,@dptr
        anl   a,#0xfc             ; clear STOP_WRITE|STOP_READ, keep the rest
        movx  @dptr,a

        mov   dptr,#0xffc3        ; I2CADR
        mov   a,#0xa0             ; EEPROM slave address, write direction (R/W=0)
        movx  @dptr,a             ; writing I2CADR puts START + address on the bus

        ; Bus-settle / device-ready delay: 16-bit counter R4:R5 = 0x00FF, run
        ; down to zero. Written as a do-while: Keil knew 0x00FF is non-zero.
        mov   r5,#0xff
        mov   r4,#0x00
    0001$:
        mov   a,r5                ; 16-bit decrement: borrow into R4 when R5 was 0
        dec   r5
        jnz   0002$
        dec   r4
    0002$:
        mov   a,r5
        orl   a,r4
        jnz   0001$

        mov   dptr,#0xffc1        ; I2CDATO
        mov   a,r7                ; sub-address, high byte
        movx  @dptr,a
    0003$:
        mov   dptr,#0xffc0        ; I2CSTA
        movx  a,@dptr
        jnb   acc.3,0003$         ; spin until XMIT_DATA_EMPTY (no timeout)

        mov   dptr,#0xffc1        ; I2CDATO
        mov   a,r6                ; sub-address, low byte
        movx  @dptr,a
    0004$:
        mov   dptr,#0xffc0
        movx  a,@dptr
        jnb   acc.3,0004$

        mov   dptr,#0xffc0        ; I2CCTL
        movx  a,@dptr
        orl   a,#0x01             ; STOP_WRITE: release the bus after this byte
        movx  @dptr,a
        inc   dptr                ; 0xFFC0 -> 0xFFC1, I2CDATO
        mov   a,r3                ; the data byte
        movx  @dptr,a

        ; Write hold. Wait for the byte to leave the transmitter, then delay a
        ; fixed 0xFFFF-iteration count for the EEPROM's internal program cycle.
        ; The counter is (re)loaded inside the wait loop, so it is armed the
        ; moment the flag is still clear -- which it always is on entry.
    0005$:
        mov   dptr,#0xffc0
        movx  a,@dptr
        jb    acc.3,0006$
        mov   a,#0xff
        mov   r4,a
        mov   r5,a                ; R4:R5 = 0xFFFF
        sjmp  0005$
    0006$:
        mov   a,r5
        orl   a,r4
        jz    0007$
        mov   a,r5
        dec   r5
        jnz   0006$
        dec   r4
        sjmp  0006$
    0007$:
        ret
    __endasm;
}
