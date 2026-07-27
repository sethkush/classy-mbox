
/* ================================================================
 * reset_vector @ CODE:0000
 * ============================================================== */

/* reset_vector — `LJMP 0x092a` \u2014 reset entry */

void reset_vector(void)

{
  keil_c51_startup();
  return;
}



/* ================================================================
 * int0_vector @ CODE:0003
 * ============================================================== */

/* int0_vector — `LJMP 0x0ddf` \u2014 USB ISR entry */

void int0_vector(void)

{
  usb_isr_int0_vecdispatch();
  return;
}



/* ================================================================
 * usb_susr_handler @ CODE:0006
 * ============================================================== */

/* usb_susr_handler — USB SUSPEND: sets `pending_action`=0x0e, RET */

void usb_susr_handler(void)

{
  pending_action = 0xe;
  return;
}



/* ================================================================
 * reti_stub_ie1 @ CODE:000a
 * ============================================================== */

/* reti_stub_ie1 — bare RETI (IE1 unused) */

void reti_stub_ie1(void)

{
  return;
}



/* ================================================================
 * tf0_vector @ CODE:000b
 * ============================================================== */

/* tf0_vector — `LJMP 0x1016` \u2014 Timer-0 tick */

void tf0_vector(void)

{
  timer0_tick_isr();
  return;
}



/* ================================================================
 * reti_stub_tf1 @ CODE:000e
 * ============================================================== */

/* reti_stub_tf1 — bare RETI (TF1 unused) */

void reti_stub_tf1(void)

{
  return;
}



/* ================================================================
 * reti_stub_si @ CODE:000f
 * ============================================================== */

/* reti_stub_si — bare RETI (UART unused) */

void reti_stub_si(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0010 @ CODE:0010
 * ============================================================== */

void FUN_CODE_0010(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0011 @ CODE:0011
 * ============================================================== */

void FUN_CODE_0011(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0012 @ CODE:0012
 * ============================================================== */

void FUN_CODE_0012(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0016 @ CODE:0016
 * ============================================================== */

void FUN_CODE_0016(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0017 @ CODE:0017
 * ============================================================== */

void FUN_CODE_0017(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0018 @ CODE:0018
 * ============================================================== */

void FUN_CODE_0018(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0019 @ CODE:0019
 * ============================================================== */

void FUN_CODE_0019(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_001a @ CODE:001a
 * ============================================================== */

void FUN_CODE_001a(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_001e @ CODE:001e
 * ============================================================== */

void FUN_CODE_001e(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_001f @ CODE:001f
 * ============================================================== */

void FUN_CODE_001f(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0020 @ CODE:0020
 * ============================================================== */

void FUN_CODE_0020(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0021 @ CODE:0021
 * ============================================================== */

void FUN_CODE_0021(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0022 @ CODE:0022
 * ============================================================== */

void FUN_CODE_0022(void)

{
  return;
}



/* ================================================================
 * usb_setup_handler @ CODE:0026
 * ============================================================== */

/* usb_setup_handler — SETUP_INT: EP0 prologue + audio-class dispatch */

void usb_setup_handler(void)

{
  char cVar1;
  undefined1 *puVar2;
  
  ep0_clear_stall_both();
  IEPCNF0 = IEPCNF0 | 0x20;
  xfer_len_lo = ep0_store_byte_and_arm_zlp(OEPCNF0 | 0x20);
  _1_5 = 0;
  xfer_len_hi = xfer_len_lo;
  puVar2 = (undefined1 *)0xff28;
  if (DAT_EXTMEM_ff28 == '\"') {
    BANK1_R5 = 1;
    _1_3 = 1;
    _1_4 = 0;
    return;
  }
  if (DAT_EXTMEM_ff28 == -0x5f) {
    FUN_CODE_0b37();
    if (_5_4 == '\0') {
      ep0_load_dptr();
      *puVar2 = 1;
    }
    else {
      ep0_load_dptr();
      *puVar2 = 2;
    }
    IEPDCNTX0 = 1;
    _1_3 = 0;
    _1_4 = 1;
    return;
  }
  if (DAT_EXTMEM_ff28 == -0x5e) {
    puVar2 = (undefined1 *)0xff2b;
    if (DAT_EXTMEM_ff2b == '\x01') {
      FUN_CODE_0b37();
      if (clock_mode_id == '\x01') {
        ep0_load_dptr();
        *puVar2 = 0;
        BANK3_R6 = BANK3_R6 + '\x01';
        if (BANK3_R6 == '\0') {
          BANK3_R5 = BANK3_R5 + '\x01';
        }
        ep0_buf_store_zero();
        BANK3_R6 = BANK3_R6 + '\x01';
        if (BANK3_R6 == '\0') {
          BANK3_R5 = BANK3_R5 + '\x01';
        }
      }
      else if (clock_mode_id == '\x02') {
        ep0_load_dptr();
        cVar1 = BANK3_R6;
        *puVar2 = 0x44;
        BANK3_R6 = BANK3_R6 + '\x01';
        if (BANK3_R6 == '\0') {
          BANK3_R5 = BANK3_R5 + '\x01';
        }
        *(undefined1 *)CONCAT11(BANK3_R5,BANK3_R6) = 0xac;
        BANK3_R6 = cVar1 + '\x02';
        if (BANK3_R6 == '\0') {
          BANK3_R5 = BANK3_R5 + '\x01';
        }
      }
      else {
        if (clock_mode_id != '\x03') goto ep0_stall_both;
        ep0_load_dptr();
        cVar1 = BANK3_R6;
        *puVar2 = 0x80;
        BANK3_R6 = BANK3_R6 + '\x01';
        if (BANK3_R6 == '\0') {
          BANK3_R5 = BANK3_R5 + '\x01';
        }
        *(undefined1 *)CONCAT11(BANK3_R5,BANK3_R6) = 0xbb;
        BANK3_R6 = cVar1 + '\x02';
        if (BANK3_R6 == '\0') {
          BANK3_R5 = BANK3_R5 + '\x01';
        }
      }
      ep0_buf_store_zero();
      IEPDCNTX0 = 3;
      _1_3 = 0;
      _1_4 = 1;
      return;
    }
  }
  else {
    if (DAT_EXTMEM_ff28 == '!') {
      if (DAT_EXTMEM_ff29 != 0) {
        xfer_len_hi = xfer_len_lo;
        BANK1_R5 = 2;
        _1_3 = 1;
        _1_4 = 0;
        _1_5 = 0;
        return;
      }
      pending_action = 0xd;
      _1_3 = 0;
      _1_4 = 0;
      return;
    }
    if ((DAT_EXTMEM_ff29 < 0xd) << 7 < '\0') {
                    /* WARNING: Could not recover jumptable at 0x011d. Too many branches */
                    /* WARNING: Treating indirect jump as call */
      (*(code *)((ushort)(DAT_EXTMEM_ff29 * '\x03') + 0x11e))();
      return;
    }
  }
ep0_stall_both:
  FUN_CODE_1001();
  return;
}



/* ================================================================
 * thunk_stall_ep0 @ CODE:0100
 * ============================================================== */

/* thunk_stall_ep0 — `LJMP 0x02ef` (stall EP0) */

void thunk_stall_ep0(void)

{
  FUN_CODE_1001();
  return;
}



/* ================================================================
 * ep0_arm_in_3bytes @ CODE:0103
 * ============================================================== */

/* ep0_arm_in_3bytes — arm 3-byte EP0 IN reply */

void ep0_arm_in_3bytes(void)

{
  IEPDCNTX0 = 3;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * usb_std_request_dispatch @ CODE:010b
 * ============================================================== */

/* usb_std_request_dispatch — Standard request dispatcher (jump table 0x011e) */

void usb_std_request_dispatch(void)

{
  if (0xc < DAT_EXTMEM_ff29) {
    FUN_CODE_1001();
    return;
  }
                    /* WARNING: Could not recover jumptable at 0x011d. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)((ushort)(DAT_EXTMEM_ff29 * '\x03') + 0x11e))();
  return;
}



/* ================================================================
 * std_clear_feature @ CODE:0145
 * ============================================================== */

/* std_clear_feature — CLEAR_FEATURE (EP0 halt only) */

void std_clear_feature(void)

{
  if ((DAT_EXTMEM_ff28 == '\x02') && (DAT_EXTMEM_ff2c == '\0')) {
    ep0_clear_stall_both();
    _1_3 = 0;
    _1_4 = 0;
    return;
  }
  FUN_CODE_1001();
  return;
}



/* ================================================================
 * std_get_configuration @ CODE:015c
 * ============================================================== */

/* std_get_configuration — GET_CONFIGURATION \u2192 1 byte */

void std_get_configuration(undefined1 *param_1)

{
  FUN_CODE_0b37();
  if (_1_6 == '\0') {
    ep0_load_dptr();
    *param_1 = 0;
  }
  else {
    ep0_load_dptr();
    *param_1 = 1;
  }
  IEPDCNTX0 = 1;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * std_get_descriptor @ CODE:0177
 * ============================================================== */

/* std_get_descriptor — GET_DESCRIPTOR (device/config/string) */

void std_get_descriptor(void)

{
  undefined1 *puVar1;
  short sVar2;
  
  puVar1 = (undefined1 *)0xff2b;
  if (DAT_EXTMEM_ff2b == '\x01') {
    BANK3_R1 = 5;
    BANK3_R2 = 0x7d;
  }
  else {
    if ((DAT_EXTMEM_ff2b == '\x02') && (sVar2 = -0xd6, DAT_EXTMEM_ff2a == '\0')) {
      BANK3_R1 = 6;
      BANK3_R2 = 0x57;
      load_dptr_from_ptr19();
      xfer_len_lo = *(undefined1 *)(sVar2 + 2);
      xfer_len_hi = *(undefined1 *)(sVar2 + 3);
      goto LAB_CODE_01e6;
    }
    if (DAT_EXTMEM_ff2b != '\x03') {
      FUN_CODE_1001();
      return;
    }
    if (DAT_EXTMEM_ff2a == '\0') {
      BANK3_R1 = 6;
      BANK3_R2 = 0x8d;
    }
    if (DAT_EXTMEM_ff2a == '\x01') {
      BANK3_R1 = 6;
      BANK3_R2 = 0x91;
    }
    puVar1 = (undefined1 *)0xff2a;
    if (DAT_EXTMEM_ff2a == '\x02') {
      BANK3_R1 = 6;
      BANK3_R2 = 0xaf;
    }
  }
  load_dptr_from_ptr19();
  xfer_len_lo = *puVar1;
  xfer_len_hi = 0;
LAB_CODE_01e6:
  ep0_clamp_len_to_wlength();
  ep0_in_stage_and_go();
  return;
}



/* ================================================================
 * std_get_interface @ CODE:01ed
 * ============================================================== */

/* std_get_interface — GET_INTERFACE \u2192 1 byte */

void std_get_interface(char param_1)

{
  char *pcVar1;
  undefined1 *puVar2;
  
  if ((_1_2 == '\x01') || (_1_6 != '\0')) {
    pcVar1 = &DAT_EXTMEM_ff2c;
    param_1 = DAT_EXTMEM_ff2c - 3;
    if (DAT_EXTMEM_ff2c < 3) {
      FUN_CODE_0b37();
      if ((*pcVar1 == '\x01') && (_1_0 != '\0')) {
        ep0_load_dptr();
        *pcVar1 = '\x01';
      }
      else {
        puVar2 = &DAT_CODE_ff2c;
        if ((DAT_EXTMEM_ff2c == 2) && (_1_1 != '\0')) {
          ep0_load_dptr();
          *puVar2 = 2;
        }
        else {
          ep0_load_dptr();
          *puVar2 = 0;
        }
      }
      IEPDCNTX0 = 1;
      _1_3 = 0;
      _1_4 = 1;
      return;
    }
  }
  FUN_CODE_1001(param_1);
  return;
}



/* ================================================================
 * std_get_status @ CODE:022f
 * ============================================================== */

/* std_get_status — GET_STATUS \u2192 0x0000 */

void std_get_status(undefined1 *param_1)

{
  FUN_CODE_0b37();
  ep0_load_dptr();
  *param_1 = 0;
  BANK3_R6 = BANK3_R6 + '\x01';
  if (BANK3_R6 == '\0') {
    BANK3_R5 = BANK3_R5 + '\x01';
  }
  ep0_buf_store_zero();
  IEPDCNTX0 = 2;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * ep0_arm_in_and_done @ CODE:0247
 * ============================================================== */

/* ep0_arm_in_and_done — write IEPDCNTX0=A, arm IN, set flags */

void ep0_arm_in_and_done(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * std_set_address @ CODE:024d
 * ============================================================== */

/* std_set_address — SET_ADDRESS (deferred write) */

void std_set_address(void)

{
  BANK1_R5 = 5;
  pending_addr = DAT_EXTMEM_ff2a;
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * std_set_configuration @ CODE:0259
 * ============================================================== */

/* std_set_configuration — SET_CONFIGURATION (0/1) */

void std_set_configuration(void)

{
  if (1 < DAT_EXTMEM_ff2a) {
    FUN_CODE_1001(DAT_EXTMEM_ff2a - 2);
    return;
  }
  if (DAT_EXTMEM_ff2a == 0) {
    _1_2 = 0;
    _1_6 = 0;
    _1_0 = 0;
    _1_1 = 0;
  }
  if (DAT_EXTMEM_ff2a == 1) {
    _1_2 = 0;
    _1_6 = 1;
    _1_0 = 0;
    _1_1 = 0;
  }
  if (DAT_EXTMEM_ff2a == 2) {
    _1_2 = 0;
    _1_6 = 1;
    _1_0 = 0;
    _1_1 = 0;
  }
  pending_action = 1;
  FUN_CODE_0b2e(0x80);
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * std_stall_unsupported @ CODE:029b
 * ============================================================== */

/* std_stall_unsupported — stall for SET_FEATURE/SET_DESCRIPTOR/SYNCH_FRAME */

void std_stall_unsupported(void)

{
  FUN_CODE_1001();
  return;
}



/* ================================================================
 * std_set_interface @ CODE:029d
 * ============================================================== */

/* std_set_interface — SET_INTERFACE (iface1/2, alt 0/1) */

void std_set_interface(void)

{
  byte bVar1;
  
  bVar1 = DAT_EXTMEM_ff2c - 3;
  if (((DAT_EXTMEM_ff2c < 3) && (bVar1 = DAT_EXTMEM_ff2a - 2, DAT_EXTMEM_ff2a < 2)) &&
     ((_1_2 == '\x01' || (_1_6 != '\0')))) {
    if (DAT_EXTMEM_ff2c == 1) {
      _1_0 = DAT_EXTMEM_ff2a != 0;
      pending_action = 2;
    }
    else {
      bVar1 = DAT_EXTMEM_ff2c;
      if (DAT_EXTMEM_ff2c != 2) goto ep0_stall_both;
      _1_1 = DAT_EXTMEM_ff2a != 0;
      pending_action = 3;
    }
    IEPDCNTX0 = 0x80;
    OEPDCNTX0 = 0x80;
    _1_3 = 0;
    _1_4 = 0;
    return;
  }
ep0_stall_both:
  FUN_CODE_1001(bVar1);
  return;
}



/* ================================================================
 * ep0_done_no_data @ CODE:02e8
 * ============================================================== */

/* ep0_done_no_data — clear bits 0x0b/0x0c, RET */

void ep0_done_no_data(void)

{
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * ep0_stall_both @ CODE:02ef
 * ============================================================== */

/* ep0_stall_both — stall EP0 both dirs (\u2192 fcn_1001) */

void ep0_stall_both(void)

{
  FUN_CODE_1001();
  return;
}



/* ================================================================
 * usb_deferred_action_dispatch @ CODE:02f3
 * ============================================================== */

/* usb_deferred_action_dispatch — main-loop deferred control-request executor (14-way) */

void usb_deferred_action_dispatch(void)

{
  ushort uVar1;
  
  if (0xd < pending_action - 1U) {
    pending_action = 0;
    return;
  }
  uVar1 = (ushort)(pending_action - 1U) * 3;
                    /* WARNING: Could not recover jumptable at 0x030b. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)(CONCAT11((char)(uVar1 >> 8) + '\x03',0xc) + (uVar1 & 0xff)))();
  return;
}



/* ================================================================
 * FUN_CODE_0336 @ CODE:0336
 * ============================================================== */

void FUN_CODE_0336(void)

{
  byte bVar1;
  
  dma0_disable();
  DMACTL1 = DMACTL1 & 0x7f;
  GLOBCTL = GLOBCTL & 0xfe;
  if ((((_1_2 == '\x01') || (_1_6 != '\0')) && (_1_0 != '\x01')) && (_1_1 != '\x01')) {
    if (_1_2 != '\0') {
      cport_cnf3_write_enable(0xac);
    }
    if (_1_6 != '\0') {
      cport_cnf3_write_enable(0xa8);
    }
    if (_5_6 != '\x01') {
      audio_hw_bringup();
    }
  }
  else {
    if ((_1_2 == '\x01') || (_1_6 != '\0')) {
      bVar1 = 1;
    }
    else {
      bVar1 = 0;
    }
    if ((bool)(bVar1 ^ 1)) {
      _5_6 = '\0';
      shiftreg_out16_p1();
    }
  }
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_038a @ CODE:038a
 * ============================================================== */

void FUN_CODE_038a(void)

{
  if (((_1_2 == '\x01') || (_1_6 != '\0')) && (_1_0 != '\0')) {
    if (_5_6 != '\x01') {
      audio_hw_bringup();
    }
    _5_5 = 0;
    ctrl_img_A = 0xff;
    _2_0 = 0;
    _2_3 = 0;
    _3_6 = 0;
    _2_7 = 0;
    shiftreg_out8_p1hi();
    _5_0 = 0;
    _5_1 = 0;
    _5_2 = 0;
    _5_3 = 0;
    _5_4 = 0;
    shiftreg_out16_p1();
    IEPCNF1 = 0xc5;
    audio_clock_set_mode(3);
    DMACTL1 = DMACTL1 | 0x80;
    if (_1_6 != '\0') {
      OEPCNF2 = 0xc5;
      DMACTL0 = DMACTL0 | 0x80;
    }
  }
  else if (((_1_2 == '\x01') || (_1_6 != '\0')) && (_1_0 != '\x01')) {
    DMACTL1 = DMACTL1 & 0x7f;
    _2_7 = 1;
    shiftreg_out8_p1hi();
    if (_1_6 != '\0') {
      dma0_disable();
    }
  }
  USBIMSK = 0xff;
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_03fd @ CODE:03fd
 * ============================================================== */

void FUN_CODE_03fd(void)

{
  if ((_1_2 == '\0' && _1_6 == '\0') || (_1_1 == '\0')) {
    if ((_1_2 != '\0' || _1_6 != '\0') && (_1_1 != '\x01')) {
      dma0_disable();
    }
  }
  else {
    if (_5_6 != '\x01') {
      audio_hw_bringup();
    }
    _5_5 = 0;
    OEPCNF2 = 0xc5;
    audio_clock_set_mode(3);
    DMACTL0 = DMACTL0 | 0x80;
  }
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_045a @ CODE:045a
 * ============================================================== */

void FUN_CODE_045a(void)

{
  _5_4 = 0;
  _2_6 = 1;
  shiftreg_out16_p1();
  shiftreg_out8_p1hi();
  audio_clock_set_mode(clock_mode_id);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0469 @ CODE:0469
 * ============================================================== */

void FUN_CODE_0469(void)

{
  _5_4 = 1;
  _2_6 = 0;
  shiftreg_out16_p1();
  shiftreg_out8_p1hi();
  audio_clock_set_mode(1);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0478 @ CODE:0478
 * ============================================================== */

void FUN_CODE_0478(void)

{
  audio_clock_set_mode(1);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_047d @ CODE:047d
 * ============================================================== */

void FUN_CODE_047d(void)

{
  audio_clock_set_mode(2);
  if (_5_4 == '\0') {
    DAT_INTMEM_2c = 0x23;
    DAT_INTMEM_2d = 0;
    cs8427_write_shadowed();
    DAT_INTMEM_2c = 0x24;
    DAT_INTMEM_2d = 0x80;
  }
  else {
    cs8427_write_reg04_val41();
    stage_ctrl_pair_12_00();
  }
  FUN_CODE_0c31(DAT_INTMEM_2d,DAT_INTMEM_2c);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_049f @ CODE:049f
 * ============================================================== */

void FUN_CODE_049f(void)

{
  audio_clock_set_mode(3);
  if (_5_4 == '\0') {
    DAT_INTMEM_2c = 0x23;
    DAT_INTMEM_2d = 0x40;
    cs8427_write_shadowed();
    DAT_INTMEM_2c = 0x24;
    DAT_INTMEM_2d = 0x80;
  }
  else {
    cs8427_write_reg04_val41();
    stage_ctrl_pair_12_00();
  }
  FUN_CODE_0c31(DAT_INTMEM_2d,DAT_INTMEM_2c);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_04c0 @ CODE:04c0
 * ============================================================== */

void FUN_CODE_04c0(void)

{
  audio_clock_set_mode(4);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_04c4 @ CODE:04c4
 * ============================================================== */

void FUN_CODE_04c4(void)

{
  audio_clock_set_mode(5);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_04c8 @ CODE:04c8
 * ============================================================== */

void FUN_CODE_04c8(void)

{
  byte bVar1;
  
  if (_5_6 != '\x01') {
    audio_hw_bringup();
  }
  _5_5 = 1;
  audio_clock_set_mode(3);
  DAT_INTMEM_2c = 4;
  DAT_INTMEM_2d = 0x41;
  FUN_CODE_0c31(0x41,4);
  bVar1 = 0x1f;
  i2c_eeprom_read_byte(0xff);
  DAT_INTMEM_2c = bVar1 ^ 0xff;
  bVar1 = 0x1f;
  i2c_eeprom_write3(DAT_INTMEM_2c);
  i2c_eeprom_read_byte(0xff);
  if (bVar1 == DAT_INTMEM_2c) {
    _2_6 = 0;
  }
  DAT_INTMEM_2d = bVar1;
  shiftreg_out8_p1hi();
  stage_ctrl_pair_12_00();
  FUN_CODE_0c31(DAT_INTMEM_2d,DAT_INTMEM_2c);
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0517 @ CODE:0517
 * ============================================================== */

void FUN_CODE_0517(void)

{
  i2c_eeprom_write3(0,0,0);
  OEPDCNTX0 = 0;
  pending_action = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0525 @ CODE:0525
 * ============================================================== */

void FUN_CODE_0525(void)

{
  byte bVar1;
  
  if ((char)((_1_6 & 1 | _1_2) << 7) < '\0') {
    ACGCTL = ACGCTL & 0x3f;
    DAT_INTMEM_25 = 0;
    ctrl_img_B0 = 0;
    shiftreg_out16_p1();
    ctrl_img_A = 0xff;
    _3_6 = 0;
    shiftreg_out8_p1hi();
    bVar1 = PCON;
    PCON = bVar1 | 1;
    USBCTL = USBCTL & 0x7f;
    USBIMSK = 0x9f;
    hw_clock_codec_init();
    usb_ep_dma_init();
    TR0 = 1;
    EX0 = 1;
    EA = 1;
    USBCTL = USBCTL | 0x80;
  }
  pending_action = 0;
  return;
}



/* ================================================================
 * cs8427_write_reg04_val41 @ CODE:0567
 * ============================================================== */

/* cs8427_write_reg04_val41 — CS8427 reg 0x04 := 0x41 */

void cs8427_write_reg04_val41(void)

{
  DAT_INTMEM_2c = 4;
  DAT_INTMEM_2d = 0x41;
  FUN_CODE_0c31(0x41,4);
  return;
}



/* ================================================================
 * cs8427_write_shadowed @ CODE:0575
 * ============================================================== */

/* cs8427_write_shadowed — CS8427 write of preloaded 0x2c/0x2d */

void cs8427_write_shadowed(void)

{
  FUN_CODE_0c31(DAT_INTMEM_2d,DAT_INTMEM_2c);
  return;
}



/* ================================================================
 * audio_clock_set_mode @ CODE:070f
 * ============================================================== */

/* audio_clock_set_mode — program ACG for clock mode R7 (1/2/3/5) */

void audio_clock_set_mode(char param_1)

{
  short sVar1;
  
  delay_lo = 0;
  DAT_INTMEM_30 = 0;
  _3_2 = 0;
  _3_3 = 0;
  mode_param = param_1;
  shiftreg_out16_p1();
  FUN_CODE_0ef4(0xffe2);
  if (mode_param == '\x02') {
    ACGFRQ1 = 0x4b;
    ACGFRQ2 = 0x6a;
    ACGFRQ0 = 0x20;
    DMATSH3 = 0x4b;
    DMACTL3 = 0x6a;
    acg2frq0_load_and_acgctl(0x20);
    clock_mode_id = 2;
  }
  else if (mode_param == '\x03') {
    acg_both_synths_24576khz();
    clock_mode_id = 3;
  }
  else {
    if (mode_param != '\x05') {
      if (mode_param == '\x01') {
        ACGCTL = 0xd;
        clock_mode_id = 1;
        DAT_INTMEM_31 = 4;
        DAT_INTMEM_32 = 0x41;
      }
      goto LAB_CODE_07a6;
    }
    GLOBCTL = GLOBCTL & 0xfe;
    CPTRXCNF4 = 1;
    sVar1 = -0x4f;
    sfr_write_then_acg_program(GLOBCTL | 1);
    *(undefined1 *)(sVar1 + 1) = 0;
    DMATSL2 = 0x10;
    _3_0 = 1;
    _3_1 = 1;
    shiftreg_out16_p1();
    clock_mode_id = 5;
  }
  DAT_INTMEM_31 = 4;
  DAT_INTMEM_32 = 0x40;
LAB_CODE_07a6:
  FUN_CODE_0c31(DAT_INTMEM_32,DAT_INTMEM_31);
  ACGCTL = ACGCTL | 0xc0;
  IEPDCNTX1 = 0;
  IEPDCNTY1 = 0;
  OEPDCNTX2 = 0;
  OEPDCNTY2 = 0;
  IEPCNF1 = 0xc5;
  OEPCNF2 = 0xc5;
  _3_2 = 1;
  _3_3 = 1;
  shiftreg_out16_p1();
  delay_lo = '\0';
  DAT_INTMEM_30 = '\0';
  do {
    DAT_INTMEM_30 = DAT_INTMEM_30 + '\x01';
    if (DAT_INTMEM_30 == '\0') {
      delay_lo = delay_lo + '\x01';
    }
  } while ((DAT_INTMEM_30 != -1) || (delay_lo != '\x0f'));
  return;
}



/* ================================================================
 * hw_clock_codec_init @ CODE:07ec
 * ============================================================== */

/* hw_clock_codec_init — MCU/codec-port/ACG cold init */

void hw_clock_codec_init(void)

{
  byte bVar1;
  
  mode_param = 0;
  delay_lo = 0;
  USBCTL = 0;
  MEMCFG = 1;
  P1 = 0;
  P3 = 0xff;
  TH0 = 0xce;
  TL0 = 0;
  TH1 = 0;
  TL1 = 0;
  TMOD = 0x11;
  TCON = 0;
  EA = 0;
  ES = 0;
  EX1 = 0;
  ET0 = 1;
  ET1 = 0;
  EX0 = 1;
  IP = 0;
  GLOBCTL = 6;
  CPTCNF1 = 0xd;
  CPTCNF2 = 0xe5;
  CPTCNF3 = 0xac;
  CPTCNF4 = 3;
  CPTCTL = 0x50;
  CPTRXCNF2 = 0x25;
  CPTRXCNF3 = 0xac;
  sfr_write_then_acg_program(3,0xffd4);
  acg_dividers_div2();
  GLOBCTL = GLOBCTL | 1;
  clock_mode_id = 3;
  ctrl_img_A = 0;
  _3_6 = 1;
  shiftreg_out8_p1hi();
  while( true ) {
    bVar1 = ~delay_lo;
    if (bVar1 == 0) {
      bVar1 = mode_param ^ 0xf;
    }
    if (bVar1 == 0) break;
    delay_lo = delay_lo + 1;
    if (delay_lo == 0) {
      mode_param = mode_param + 1;
    }
  }
  ctrl_img_A = 0xff;
  _2_0 = 0;
  _2_3 = 0;
  _3_6 = 0;
  shiftreg_out8_p1hi();
  DAT_INTMEM_25 = 0;
  ctrl_img_B0 = 0;
  shiftreg_out16_p1();
  return;
}



/* ================================================================
 * usb_ep_dma_init @ CODE:0891
 * ============================================================== */

/* usb_ep_dma_init — EP0 + iso EP1/EP2 + DMA setup, USBIMSK/USBFADR */

void usb_ep_dma_init(void)

{
  OEPBBAX0 = 0x42;
  IEPBBAX0 = 0x43;
  OEPDCNTX0 = 0;
  IEPDCNTX0 = 0;
  OEPDCNTY0 = 0;
  IEPDCNTY0 = 0;
  OEPBSIZ0 = 1;
  IEPBSIZ0 = 1;
  OEPCNF0 = 0x84;
  IEPCNF0 = 0x84;
  OEPBBAX2 = 0x44;
  IEPBBAX1 = 0x94;
  OEPBSIZ2 = 0x50;
  IEPBSIZ1 = 0x50;
  OEPDCNTX2 = 0;
  IEPDCNTX1 = 0;
  OEPCNF2 = 0xc5;
  IEPCNF1 = 0xc5;
  DMATSL0 = 3;
  DMATSH0 = 0x80;
  DMATSL1 = 3;
  DMATSH1 = 0x80;
  DMACTL0 = 2;
  DMACTL1 = 9;
  USBIMSK = 0x9f;
  USBFADR = 0;
  _1_2 = 0;
  _1_6 = 0;
  _1_0 = 0;
  _1_1 = 0;
  xfer_len_lo = 0;
  xfer_len_hi = 0;
  BANK1_R4 = 0xfe;
  pending_action = 0;
  return;
}



/* ================================================================
 * keil_c51_startup @ CODE:092a
 * ============================================================== */

/* keil_c51_startup — ?C_STARTUP: clear IRAM, SP=0x32, run init, \u2192 main */

void keil_c51_startup(void)

{
  char cVar1;
  undefined1 *puVar2;
  byte bVar3;
  byte *pbVar4;
  byte bVar5;
  byte bVar6;
  byte *pbVar7;
  byte *pbVar8;
  
  puVar2 = &DAT_INTMEM_7f;
  do {
    *puVar2 = 0;
    puVar2 = puVar2 + -1;
  } while (puVar2 != (undefined1 *)0x0);
  SP = 0x32;
  pbVar7 = BYTE_ARRAY_CODE_0fba;
  while( true ) {
    bVar3 = 1;
    bVar5 = *pbVar7;
    if (bVar5 == 0) break;
    bVar6 = bVar5 & 0x3f;
    pbVar8 = pbVar7 + 1;
    if (bVar6 >> 5 != 0) {
      bVar3 = bVar5 & 0x1f;
      bVar6 = pbVar7[1];
      pbVar8 = pbVar7 + 2;
      if (bVar6 != 0) {
        bVar3 = bVar3 + 1;
      }
    }
    pbVar7 = pbVar8;
    cVar1 = CARRY1(bVar5 & 0xc0,bVar5 & 0xc0) << 7;
    if ((bVar5 & 0x40) == 0) {
      pbVar4 = (byte *)*pbVar7;
      pbVar7 = pbVar7 + 1;
      do {
        bVar3 = *pbVar7;
        pbVar7 = pbVar7 + 1;
        if (cVar1 < '\0') {
          *(byte *)ZEXT12(pbVar4) = bVar3;
        }
        else {
          *pbVar4 = bVar3;
        }
        pbVar4 = pbVar4 + '\x01';
        bVar6 = bVar6 - 1;
      } while (bVar6 != 0);
    }
    else if (cVar1 < '\0') {
      do {
        bVar3 = *pbVar7;
        pbVar7 = pbVar7 + 1;
        pbVar4 = (byte *)((bVar3 & 0x7f) >> 3 | 0x20);
        bVar5 = *(byte *)((ushort)((bVar3 & 7) + 0xc) + 0x95d);
        if ((char)bVar3 < '\0') {
          bVar5 = bVar5 | *pbVar4;
        }
        else {
          bVar5 = ~bVar5 & *pbVar4;
        }
        *pbVar4 = bVar5;
        bVar6 = bVar6 - 1;
      } while (bVar6 != 0);
    }
    else {
      pbVar8 = *(byte **)pbVar7;
      pbVar7 = pbVar7 + 2;
      do {
        do {
          bVar5 = *pbVar7;
          pbVar7 = pbVar7 + 1;
          *pbVar8 = bVar5;
          pbVar8 = pbVar8 + 1;
          bVar6 = bVar6 - 1;
        } while (bVar6 != 0);
        bVar3 = bVar3 - 1;
      } while (bVar3 != 0);
    }
  }
  main_loop();
  return;
}



/* ================================================================
 * keil_c_init_interpreter @ CODE:0939
 * ============================================================== */

/* keil_c_init_interpreter — ?C_INIT: interpret table at 0x0FBA */

void keil_c_init_interpreter(byte *param_1,byte param_2)

{
  byte *pbVar1;
  byte bVar2;
  byte bVar3;
  char in_PSW;
  byte *pbVar4;
  
  do {
    pbVar1 = (byte *)*param_1;
    param_1 = param_1 + 1;
    do {
      bVar2 = *param_1;
      param_1 = param_1 + 1;
      if (in_PSW < '\0') {
        *(byte *)ZEXT12(pbVar1) = bVar2;
      }
      else {
        *pbVar1 = bVar2;
      }
      pbVar1 = pbVar1 + '\x01';
      param_2 = param_2 - 1;
    } while (param_2 != 0);
    while( true ) {
      bVar2 = 1;
      bVar3 = *param_1;
      if (bVar3 == 0) {
        main_loop();
        return;
      }
      param_2 = bVar3 & 0x3f;
      pbVar4 = param_1 + 1;
      if (param_2 >> 5 != 0) {
        bVar2 = bVar3 & 0x1f;
        param_2 = param_1[1];
        pbVar4 = param_1 + 2;
        if (param_2 != 0) {
          bVar2 = bVar2 + 1;
        }
      }
      param_1 = pbVar4;
      in_PSW = CARRY1(bVar3 & 0xc0,bVar3 & 0xc0) << 7;
      if ((bVar3 & 0x40) == 0) break;
      if (in_PSW < '\0') {
        do {
          bVar2 = *param_1;
          param_1 = param_1 + 1;
          pbVar1 = (byte *)((bVar2 & 0x7f) >> 3 | 0x20);
          bVar3 = *(byte *)((ushort)((bVar2 & 7) + 0xc) + 0x95d);
          if ((char)bVar2 < '\0') {
            bVar3 = bVar3 | *pbVar1;
          }
          else {
            bVar3 = ~bVar3 & *pbVar1;
          }
          *pbVar1 = bVar3;
          param_2 = param_2 - 1;
        } while (param_2 != 0);
      }
      else {
        pbVar4 = *(byte **)param_1;
        param_1 = param_1 + 2;
        do {
          do {
            bVar3 = *param_1;
            param_1 = param_1 + 1;
            *pbVar4 = bVar3;
            pbVar4 = pbVar4 + 1;
            param_2 = param_2 - 1;
          } while (param_2 != 0);
          bVar2 = bVar2 - 1;
        } while (bVar2 != 0);
      }
    }
  } while( true );
}



/* ================================================================
 * audio_hw_bringup @ CODE:09b6
 * ============================================================== */

/* audio_hw_bringup — ACG + external-latch + codec register bring-up */

void audio_hw_bringup(void)

{
  char cVar1;
  
  DAT_INTMEM_25 = 0;
  ctrl_img_B0 = 0;
  _5_6 = 1;
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  shiftreg_out16_p1();
  acg_both_synths_24576khz();
  acg_dividers_div2();
  clock_mode_id = 3;
  ACGCTL = ACGCTL | 0xc0;
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  _3_2 = 1;
  _3_3 = 1;
  shiftreg_out16_p1();
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  _5_7 = 1;
  _3_4 = 1;
  shiftreg_out16_p1();
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  _5_7 = 0;
  shiftreg_out16_p1();
  _5_7 = 1;
  shiftreg_out16_p1();
  FUN_CODE_0c31(0,4);
  FUN_CODE_0c31(0x10,0x13);
  FUN_CODE_0c31(0,4);
  FUN_CODE_0c31(0x40,4);
  FUN_CODE_0c31(1,1);
  FUN_CODE_0c31(0x20,2);
  FUN_CODE_0c31(0xc,3);
  FUN_CODE_0c31(5,5);
  FUN_CODE_0c31(5,6);
  FUN_CODE_0c31(0xff,0x11);
  return;
}



/* ================================================================
 * main_loop @ CODE:0a3f
 * ============================================================== */

/* main_loop — post-init forever loop */

void main_loop(byte param_1)

{
  char cVar1;
  
  p3_1_latch = '\0';
  startup_delay_ctr = 0xff;
  DAT_INTMEM_29 = -1;
  DAT_INTMEM_2a = 0;
  DAT_INTMEM_2b = 0x10;
  EA = 0;
  USBIMSK = 0;
  _4_2 = 0;
  hw_clock_codec_init();
  usb_ep_dma_init();
  while (cVar1 = DAT_INTMEM_29, (byte)-(((DAT_INTMEM_29 == '\0') << 7) >> 7) <= startup_delay_ctr) {
    DAT_INTMEM_29 = DAT_INTMEM_29 + -1;
    if (cVar1 == '\0') {
      startup_delay_ctr = startup_delay_ctr - 1;
    }
  }
  TR0 = 1;
  EA = 1;
  USBCTL = USBCTL | 0x80;
  do {
    while (_4_0 != '\x01') {
      if (pending_action != '\0') {
        usb_deferred_action_dispatch();
      }
    }
    p3_edge_poll_dispatch();
    if ((param_1 & 1) != 0) {
      shiftreg_out8_p1hi();
      shiftreg_out16_p1();
    }
    if ((_0_1 != '\x01') && (p3_1_latch == '\0')) {
      p3_1_latch = '\x01';
      pending_action = '\v';
      usb_deferred_action_dispatch();
    }
    if ((_0_1 != '\0') && (p3_1_latch == '\x01')) {
      p3_1_latch = '\0';
      pending_action = '\f';
      usb_deferred_action_dispatch();
    }
    _4_0 = '\0';
  } while( true );
}



/* ================================================================
 * ep0_in_send_chunk @ CODE:0abb
 * ============================================================== */

/* ep0_in_send_chunk — EP0 IN data-stage \u22648-byte copy engine */

void ep0_in_send_chunk(undefined1 *param_1)

{
  undefined1 uVar1;
  
  pkt_bytecount = 0;
  FUN_CODE_0b37();
  do {
    if ((xfer_len_lo == '\0') << 7 < '\0') break;
    load_dptr_from_ptr19(xfer_len_lo + -1);
    uVar1 = ep0_load_dptr(*param_1);
    *param_1 = uVar1;
    BANK3_R6 = BANK3_R6 + '\x01';
    if (BANK3_R6 == '\0') {
      BANK3_R5 = BANK3_R5 + '\x01';
    }
    BANK3_R2 = BANK3_R2 + '\x01';
    if (BANK3_R2 == '\0') {
      BANK3_R1 = BANK3_R1 + '\x01';
    }
    xfer_len_lo = xfer_len_lo + -1;
    if ((xfer_len_lo == '\0') && (xfer_len_hi != '\0')) {
      xfer_len_lo = -1;
      xfer_len_hi = xfer_len_hi + -1;
    }
    pkt_bytecount = pkt_bytecount + 1;
  } while (pkt_bytecount != 8);
  IEPDCNTX0 = pkt_bytecount | 0x80;
  _1_3 = 1;
  _1_4 = 0;
  if ((xfer_len_lo == '\0') && (xfer_len_hi == '\0')) {
    if ((pkt_bytecount == 8) && (_1_5 != '\0')) {
      _1_3 = 1;
      _1_4 = 0;
      return;
    }
    _1_3 = 0;
    _1_4 = 1;
  }
  return;
}



/* ================================================================
 * FUN_CODE_0b1f @ CODE:0b1f
 * ============================================================== */

void FUN_CODE_0b1f(void)

{
  BANK3_R5 = 0xfa;
  BANK3_R6 = 0x10;
  return;
}



/* ================================================================
 * ep0_load_dptr @ CODE:0b25
 * ============================================================== */

/* ep0_load_dptr — DPTR := cursor (0x1d:0x1e) */

void ep0_load_dptr(void)

{
  return;
}



/* ================================================================
 * ep0_store_byte_and_arm_zlp @ CODE:0b2c
 * ============================================================== */

/* ep0_store_byte_and_arm_zlp — store A, then IEPDCNTX0=OEPDCNTX0=0 (or =A via 0x0b2e) */

void ep0_store_byte_and_arm_zlp(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b2e @ CODE:0b2e
 * ============================================================== */

void FUN_CODE_0b2e(undefined1 param_1)

{
  IEPDCNTX0 = param_1;
  OEPDCNTX0 = param_1;
  return;
}



/* ================================================================
 * FUN_CODE_0b37 @ CODE:0b37
 * ============================================================== */

void FUN_CODE_0b37(void)

{
  BANK3_R5 = 0xfa;
  BANK3_R6 = 0x18;
  return;
}



/* ================================================================
 * ep0_clear_stall_both @ CODE:0b3e
 * ============================================================== */

/* ep0_clear_stall_both — IEPCNF0/OEPCNF0 &= ~0x08 */

void ep0_clear_stall_both(void)

{
  IEPCNF0 = IEPCNF0 & 0xf7;
  OEPCNF0 = OEPCNF0 & 0xf7;
  return;
}



/* ================================================================
 * ep0_clear_stall_toggle @ CODE:0b4d
 * ============================================================== */

/* ep0_clear_stall_toggle — IEPCNF0 &= ~0x28 (OUT half is a no-op \u2014 see \u00a72.5.5) */

byte ep0_clear_stall_toggle(void)

{
  IEPCNF0 = IEPCNF0 & 0xd7;
  return OEPCNF0 & 0xd7;
}



/* ================================================================
 * ep0_buf_store_zero @ CODE:0b5b
 * ============================================================== */

/* ep0_buf_store_zero — store 0 at 0x1d:A */

void ep0_buf_store_zero(undefined1 param_1)

{
  *(undefined1 *)CONCAT11(BANK3_R5,param_1) = 0;
  return;
}



/* ================================================================
 * ep0_in_stage_and_go @ CODE:0b63
 * ============================================================== */

/* ep0_in_stage_and_go — call `fcn_0abb`, clear IEPDCNTX0 NAK */

void ep0_in_stage_and_go(void)

{
  ep0_in_send_chunk();
  IEPDCNTX0 = IEPDCNTX0 & 0x7f;
  return;
}



/* ================================================================
 * load_dptr_from_ptr19 @ CODE:0b6e
 * ============================================================== */

/* load_dptr_from_ptr19 — DPTR := desc_ptr (0x19:0x1a) */

void load_dptr_from_ptr19(void)

{
  return;
}



/* ================================================================
 * ep0_flush_arm @ CODE:0b75
 * ============================================================== */

/* ep0_flush_arm — OEPDCNTX0=0, IEPDCNTX0=0 (status stage) */

void ep0_flush_arm(void)

{
  OEPDCNTX0 = 0;
  IEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * udiv16 @ CODE:0b7f
 * ============================================================== */

/* udiv16 — unsigned 16\u00f716 divide runtime helper */

byte udiv16(char param_1,byte param_2,byte param_3,byte param_4)

{
  bool bVar1;
  byte bVar2;
  char cVar3;
  byte bVar4;
  byte bVar5;
  byte bVar6;
  
  if (param_1 != '\0') {
    bVar2 = 0;
    cVar3 = '\b';
    do {
      bVar1 = CARRY1(param_4,param_4);
      param_4 = param_4 * '\x02';
      bVar4 = param_3 << 1 | bVar1;
      bVar6 = bVar2 << 1 | param_3 >> 7;
      bVar5 = param_1 - (((bVar4 < param_2 - ((char)bVar2 >> 7)) << 7) >> 7);
      bVar2 = bVar6;
      if (bVar6 >= bVar5) {
        bVar4 = bVar4 - (param_2 - (((bVar6 < bVar5) << 7) >> 7));
        param_4 = param_4 + 1;
        bVar2 = bVar6 - bVar5;
      }
      cVar3 = cVar3 + -1;
      param_3 = bVar4;
    } while (cVar3 != '\0');
    return bVar4;
  }
  if (param_3 == 0) {
    if (param_2 != 0) {
      param_4 = param_4 / param_2;
    }
    return param_4;
  }
  bVar2 = 0;
  bVar5 = param_3;
  if (param_2 != 0) {
    bVar5 = param_3 / param_2;
    bVar2 = param_3 % param_2;
  }
  cVar3 = OV;
  if (cVar3 != '\x01') {
    cVar3 = '\b';
    bVar5 = bVar2;
LAB_CODE_0bbd:
    do {
      bVar1 = CARRY1(param_4,param_4);
      param_4 = param_4 * '\x02';
      bVar2 = bVar5 << 1 | bVar1;
      if ((char)bVar5 < '\0') {
        bVar5 = bVar2 - param_2;
      }
      else {
        bVar4 = param_2 - ((char)bVar5 >> 7);
        bVar6 = bVar2 - bVar4;
        bVar5 = bVar6;
        if (bVar2 < bVar4) {
          cVar3 = cVar3 + -1;
          bVar5 = bVar2;
          if (cVar3 == '\0') {
            return bVar6;
          }
          goto LAB_CODE_0bbd;
        }
      }
      param_4 = param_4 + 1;
      cVar3 = cVar3 + -1;
    } while (cVar3 != '\0');
  }
  return bVar5;
}



/* ================================================================
 * jmp_via_r2r1 @ CODE:0bd4
 * ============================================================== */

/* jmp_via_r2r1 — computed jump to R2:R1 */

void jmp_via_r2r1(undefined1 param_1,undefined1 param_2)

{
                    /* WARNING: Could not recover jumptable at 0x0bd9. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)CONCAT11(param_2,param_1))();
  return;
}



/* ================================================================
 * i2c_eeprom_write3 @ CODE:0bda
 * ============================================================== */

/* i2c_eeprom_write3 — write 3 bytes (addr-hi, addr-lo, data) to EEPROM 0xA0 */

void i2c_eeprom_write3(undefined1 param_1)

{
  bool bVar1;
  byte bVar2;
  char cVar3;
  char cVar4;
  char cVar5;
  
  bVar2 = I2CSTA;
  I2CADR = 0xa0;
  cVar3 = '\0';
  cVar5 = -1;
  do {
    cVar4 = cVar5 + -1;
    if (cVar5 == '\0') {
      cVar3 = cVar3 + -1;
    }
    cVar5 = cVar4;
  } while (cVar4 != '\0' || cVar3 != '\0');
  do {
  } while ((I2CSTA >> 3 & 1) == 0);
  do {
  } while ((I2CSTA >> 3 & 1) == 0);
  I2CSTA = I2CSTA & 0xfc | 1;
  I2CDATO = param_1;
  while ((bVar2 & 8) != 8) {
    cVar3 = -1;
    cVar4 = cVar3;
  }
  while (cVar4 != '\0' || cVar3 != '\0') {
    cVar5 = cVar4 + -1;
    bVar1 = cVar4 == '\0';
    cVar4 = cVar5;
    if (bVar1) {
      cVar3 = cVar3 + -1;
    }
  }
  return;
}



/* ================================================================
 * FUN_CODE_0c31 @ CODE:0c31
 * ============================================================== */

void FUN_CODE_0c31(void)

{
  char cVar1;
  char cVar2;
  char cVar3;
  byte bVar4;
  
  cVar3 = '\b';
  cVar2 = '\x01';
  _5_7 = 0;
  shiftreg_out16_p1(BANK0_R7,0x20);
  while( true ) {
    while( true ) {
      for (; cVar3 != '\0'; cVar3 = cVar3 + -1) {
        cVar1 = '\x02';
        bVar4 = BANK0_R3;
        while (cVar1 = cVar1 + -1, cVar1 != '\0') {
          bVar4 = bVar4 << 1 | bVar4 >> 7;
        }
        if ((bVar4 & 1) == 0) {
          bVar4 = P1;
          P1 = bVar4 & 0xef;
        }
        else {
          bVar4 = P1;
          P1 = bVar4 | 0x10;
        }
        bVar4 = P1;
        P1 = bVar4 | 8;
        bVar4 = P1;
        P1 = bVar4 & 0xf7;
      }
      if (cVar2 != '\x01') break;
      cVar2 = '\x02';
      cVar3 = '\b';
    }
    if (cVar2 != '\x02') break;
    cVar2 = '\x03';
    cVar3 = '\b';
  }
  _5_7 = 1;
  shiftreg_out16_p1();
  return;
}



/* ================================================================
 * oep0_int_handler @ CODE:0cc7
 * ============================================================== */

/* oep0_int_handler — OEP0_INT: EP0 OUT data-stage for class requests */

void oep0_int_handler(char *param_1)

{
  char cVar1;
  
  if (_1_3 != '\0') {
    if (BANK1_R5 == '\x01') {
      FUN_CODE_0b1f();
      cVar1 = *param_1;
      if (cVar1 == 'D') {
        pending_action = 7;
      }
      if (cVar1 == -0x80) {
        pending_action = 8;
      }
      if (cVar1 == '\0') {
        pending_action = 6;
      }
    }
    if (BANK1_R5 == '\x02') {
      FUN_CODE_0b1f();
      if (*param_1 == '\x01') {
        pending_action = 4;
      }
      else {
        pending_action = 5;
      }
    }
    _1_3 = 0;
    _1_4 = 0;
    IEPCNF0 = IEPCNF0 | 0x20;
    ep0_flush_arm();
    return;
  }
  ep0_clear_stall_toggle();
  ep0_store_byte_and_arm_zlp();
  return;
}



/* ================================================================
 * oep0_clear_stall_and_rearm @ CODE:0d0a
 * ============================================================== */

/* oep0_clear_stall_and_rearm — tail of `oep0_int_handler` (no OUT data expected) */

void oep0_clear_stall_and_rearm(void)

{
  ep0_clear_stall_toggle();
  ep0_store_byte_and_arm_zlp();
  return;
}



/* ================================================================
 * i2c_eeprom_read_byte @ CODE:0d11
 * ============================================================== */

/* i2c_eeprom_read_byte — random-read 1 byte from EEPROM 0xA0 \u2192 R7 */

undefined1 i2c_eeprom_read_byte(void)

{
  do {
  } while ((I2CSTA >> 3 & 1) == 0);
  do {
  } while ((I2CSTA >> 3 & 1) == 0);
  BANK0_R6 = BANK0_R6 | 1;
  I2CADR = 0xa0;
  I2CDATO = 0;
  I2CSTA = I2CSTA & 0xfc | 2;
  do {
  } while (-1 < (char)I2CSTA);
  return I2CDATI;
}



/* ================================================================
 * sof_int_handler @ CODE:0d58
 * ============================================================== */

/* sof_int_handler — SOF_INT: audio-OUT DMA 6-byte alignment resync */

void sof_int_handler(void)

{
  char cVar1;
  char cVar2;
  byte bVar3;
  
  cVar1 = '\0';
  bVar3 = DMABCNT0L ^ BANK3_R4;
  if (bVar3 == 0) {
    bVar3 = DMABCNT0H ^ BANK3_R3;
  }
  if (bVar3 != 0) {
    BANK3_R3 = DMABCNT0H;
    BANK3_R4 = DMABCNT0L;
    cVar2 = '\x06';
    udiv16();
    if (cVar2 != '\0' || cVar1 != '\0') {
      OEPDCNTX2 = 0;
      OEPDCNTY2 = 0;
      OEPCNF2 = 0xc5;
      DMACTL0 = DMACTL0 & 0x7f | 0x80;
    }
  }
  return;
}



/* ================================================================
 * ep0_clamp_len_to_wlength @ CODE:0d9e
 * ============================================================== */

/* ep0_clamp_len_to_wlength — clamp EP0 IN length to wLength, set short flag */

byte ep0_clamp_len_to_wlength(void)

{
  byte bVar1;
  
  if ((DAT_EXTMEM_ff2f + 1 <= xfer_len_hi) ||
     ((DAT_EXTMEM_ff2f == xfer_len_hi && (DAT_EXTMEM_ff2e + 1 <= xfer_len_lo)))) {
    xfer_len_lo = DAT_EXTMEM_ff2e;
    xfer_len_hi = DAT_EXTMEM_ff2f;
    _1_5 = 0;
  }
  bVar1 = xfer_len_lo - DAT_EXTMEM_ff2e;
  if (((xfer_len_lo < DAT_EXTMEM_ff2e) << 7 < '\0') ||
     ((bVar1 = DAT_EXTMEM_ff2e, DAT_EXTMEM_ff2e == xfer_len_lo &&
      (bVar1 = xfer_len_hi - DAT_EXTMEM_ff2f, xfer_len_hi < DAT_EXTMEM_ff2f)))) {
    _1_5 = 1;
  }
  return bVar1;
}



/* ================================================================
 * usb_isr_int0_vecdispatch @ CODE:0ddf
 * ============================================================== */

/* usb_isr_int0_vecdispatch — INT0 USB ISR: read VECINT, dispatch via 0x0c7d table */

undefined1 usb_isr_int0_vecdispatch(undefined1 param_1)

{
  undefined1 *puVar1;
  
  EA = 0;
  puVar1 = (undefined1 *)
           CONCAT11('\f' - (((0x82U < (byte)(VECINT * '\x02')) << 7) >> 7),VECINT * '\x02' + 0x7d);
  jmp_via_r2r1(puVar1[1],BANK2_R6,*puVar1);
  VECINT = 0;
  EA = 1;
  return param_1;
}



/* ================================================================
 * panel_state_cycle_A @ CODE:0e1b
 * ============================================================== */

/* panel_state_cycle_A — 3-state cycle, channel-A control/LED outputs */

void panel_state_cycle_A(void)

{
  if (_5_0 == '\x01') {
    if (_5_2 == '\0') {
      _5_0 = 0;
      _5_2 = 0;
      _2_0 = 0;
      _2_1 = 1;
      _2_2 = 1;
    }
    else {
      _5_0 = 1;
      _5_2 = 0;
      _2_0 = 1;
      _2_1 = 1;
      _2_2 = 0;
    }
  }
  else {
    _5_0 = 1;
    _5_2 = 1;
    _2_0 = 1;
    _2_1 = 0;
    _2_2 = 1;
  }
  if (_5_4 != '\x01') {
    _2_6 = 1;
  }
  if (_5_4 != '\0') {
    _2_6 = 0;
  }
  if (_5_5 != '\0') {
    _2_6 = 0;
  }
  return;
}



/* ================================================================
 * shiftreg_out16_p1 @ CODE:0e56
 * ============================================================== */

/* shiftreg_out16_p1 — bit-bang 16 bits (0x23,0x25) on P1.0/P1.2/P1.1 */

void shiftreg_out16_p1(void)

{
  byte bVar1;
  bool bVar2;
  char cVar3;
  char cVar4;
  byte bVar5;
  
  cVar4 = '\b';
  _6_0 = 1;
  bVar2 = true;
  bVar5 = ctrl_img_B0;
  while( true ) {
    for (; cVar4 != '\0'; cVar4 = cVar4 + -1) {
      cVar3 = '\x02';
      while (cVar3 = cVar3 + -1, cVar3 != '\0') {
        bVar5 = bVar5 << 1 | bVar5 >> 7;
      }
      if ((bVar5 & 1) == 0) {
        bVar1 = P1;
        P1 = bVar1 & 0xfe;
      }
      else {
        bVar1 = P1;
        P1 = bVar1 | 1;
      }
      bVar1 = P1;
      P1 = bVar1 | 4;
      bVar1 = P1;
      P1 = bVar1 & 0xfb;
    }
    if (!bVar2) break;
    _6_0 = 0;
    bVar2 = false;
    cVar4 = '\b';
    bVar5 = DAT_INTMEM_25;
  }
  bVar5 = P1;
  P1 = bVar5 | 2;
  bVar5 = P1;
  P1 = bVar5 & 0xfd;
  return;
}



/* ================================================================
 * panel_state_cycle_B @ CODE:0e8f
 * ============================================================== */

/* panel_state_cycle_B — 3-state cycle, channel-B control/LED outputs */

void panel_state_cycle_B(void)

{
  if (_5_1 == '\x01') {
    if (_5_3 == '\0') {
      _5_1 = 0;
      _5_3 = 0;
      _2_3 = 0;
      _2_4 = 1;
      _2_5 = 1;
    }
    else {
      _5_1 = 1;
      _5_3 = 0;
      _2_3 = 1;
      _2_4 = 1;
      _2_5 = 0;
    }
  }
  else {
    _5_1 = 1;
    _5_3 = 1;
    _2_3 = 1;
    _2_4 = 0;
    _2_5 = 1;
  }
  if (_5_4 != '\x01') {
    _2_6 = 1;
  }
  if (_5_4 != '\0') {
    _2_6 = 0;
  }
  if (_5_5 != '\0') {
    _2_6 = 0;
  }
  return;
}



/* ================================================================
 * sfr_write_then_acg_program @ CODE:0ec7
 * ============================================================== */

/* sfr_write_then_acg_program — write caller A@DPTR, fall into ACG program */

void sfr_write_then_acg_program(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  ACGFRQ1 = 0xa8;
  ACGFRQ2 = 0x61;
  ACGFRQ0 = 0xf;
  DMATSH3 = 0xa8;
  DMACTL3 = 0x61;
  DMATSL3 = 0xf;
  ACGCTL = 6;
  return;
}



/* ================================================================
 * acg_both_synths_24576khz @ CODE:0ec8
 * ============================================================== */

/* acg_both_synths_24576khz — ACG1+ACG2 = 24.576 MHz */

void acg_both_synths_24576khz(void)

{
  ACGFRQ1 = 0xa8;
  ACGFRQ2 = 0x61;
  ACGFRQ0 = 0xf;
  DMATSH3 = 0xa8;
  DMACTL3 = 0x61;
  DMATSL3 = 0xf;
  ACGCTL = 6;
  return;
}



/* ================================================================
 * acg2frq0_load_and_acgctl @ CODE:0ee8
 * ============================================================== */

/* acg2frq0_load_and_acgctl — load ACG2FRQ0, ACGCTL=0x06 */

void acg2frq0_load_and_acgctl(undefined1 param_1)

{
  DMATSL3 = param_1;
  ACGCTL = 6;
  return;
}



/* ================================================================
 * acg_dividers_div2 @ CODE:0ef3
 * ============================================================== */

/* acg_dividers_div2 — ACG1DCTL/ACG2DCTL = 0x10 (\u00f72) */

void acg_dividers_div2(short param_1)

{
  *(undefined1 *)(param_1 + 1) = 0x10;
  DMATSL2 = 0x10;
  return;
}



/* ================================================================
 * FUN_CODE_0ef4 @ CODE:0ef4
 * ============================================================== */

void FUN_CODE_0ef4(undefined1 *param_1)

{
  *param_1 = 0x10;
  DMATSL2 = 0x10;
  return;
}



/* ================================================================
 * shiftreg_out8_p1hi @ CODE:0efc
 * ============================================================== */

/* shiftreg_out8_p1hi — bit-bang byte 0x22 on P1.7/P1.5/P1.6 */

void shiftreg_out8_p1hi(void)

{
  byte bVar1;
  char cVar2;
  char cVar3;
  byte bVar4;
  
  cVar3 = '\b';
  bVar4 = P1;
  P1 = bVar4 & 0xbf;
  bVar4 = ctrl_img_A;
  do {
    cVar2 = '\x02';
    while (cVar2 = cVar2 + -1, cVar2 != '\0') {
      bVar4 = bVar4 << 1 | bVar4 >> 7;
    }
    if ((bVar4 & 1) == 0) {
      bVar1 = P1;
      P1 = bVar1 & 0x7f;
    }
    else {
      bVar1 = P1;
      P1 = bVar1 | 0x80;
    }
    bVar1 = P1;
    P1 = bVar1 | 0x20;
    bVar1 = P1;
    P1 = bVar1 & 0xdf;
    cVar3 = cVar3 + -1;
  } while (cVar3 != '\0');
  if (_3_6 != '\0') {
    bVar4 = P1;
    P1 = bVar4 | 0xc0;
    return;
  }
  bVar4 = P1;
  P1 = bVar4 & 0x7f;
  bVar4 = P1;
  P1 = bVar4 | 0x40;
  bVar4 = P1;
  P1 = bVar4 & 0xbf;
  return;
}



/* ================================================================
 * p3_edge_poll_dispatch @ CODE:0f31
 * ============================================================== */

/* p3_edge_poll_dispatch — poll P3, dispatch on P3.5/P3.3/P3.4 edges */

void p3_edge_poll_dispatch(void)

{
  byte bVar1;
  
  bVar1 = P3;
  if (bVar1 == p3_shadow) {
    return;
  }
  if ((_0_5 != '\x01') && ((bVar1 >> 5 & 1) != 0)) {
    toggle_bit1E_state(0);
    BANK0_R7 = BANK0_R7 | 1;
  }
  if ((_0_3 != '\x01') && ((bVar1 >> 3 & 1) != 0)) {
    panel_state_cycle_A();
    BANK0_R7 = BANK0_R7 | 1;
  }
  if ((_0_4 != '\x01') && ((bVar1 >> 4 & 1) != 0)) {
    panel_state_cycle_B();
    BANK0_R7 = BANK0_R7 | 1;
  }
  p3_shadow = bVar1;
  return;
}



/* ================================================================
 * usb_rstr_handler @ CODE:0f64
 * ============================================================== */

/* usb_rstr_handler — RSTR_INT: bus-reset EP0/USB re-init */

void usb_rstr_handler(void)

{
  OEPDCNTX2 = ep0_flush_arm();
  IEPDCNTX1 = OEPDCNTX2;
  USBFADR = OEPDCNTX2;
  OEPCNF0 = 0x84;
  IEPCNF0 = 0x84;
  USBCTL = USBCTL | 0xc0;
  _1_2 = 0;
  _1_6 = 0;
  _1_0 = 0;
  _1_1 = 0;
  USBIMSK = 0x9f;
  return;
}



/* ================================================================
 * ep0_in_done_handler @ CODE:0f91
 * ============================================================== */

/* ep0_in_done_handler — IEP0_INT: EP0 IN complete (chunk / status / SET_ADDR) */

void ep0_in_done_handler(void)

{
  byte bVar1;
  
  if (_1_3 != '\0') {
    ep0_in_stage_and_go();
    return;
  }
  if (_1_4 == '\0') {
    if (BANK1_R5 == '\x05') {
      USBFADR = pending_addr;
      BANK1_R5 = '\0';
    }
    bVar1 = ep0_clear_stall_toggle();
  }
  else {
    _1_4 = '\0';
    bVar1 = OEPCNF0 | 0x20;
  }
  ep0_store_byte_and_arm_zlp(bVar1);
  return;
}



/* ================================================================
 * cport_cnf3_write_enable @ CODE:0fe2
 * ============================================================== */

/* cport_cnf3_write_enable — CPTCNF3/CPTRXCNF3 = A, GLOBCTL */

void cport_cnf3_write_enable(undefined1 param_1)

{
  CPTCNF3 = param_1;
  CPTRXCNF3 = param_1;
  GLOBCTL = GLOBCTL | 1;
  return;
}



/* ================================================================
 * dma0_disable @ CODE:0ff2
 * ============================================================== */

/* dma0_disable — DMACTL0 &= ~DMAEN */

void dma0_disable(void)

{
  DMACTL0 = DMACTL0 & 0x7f;
  return;
}



/* ================================================================
 * stage_ctrl_pair_12_00 @ CODE:0ffa
 * ============================================================== */

/* stage_ctrl_pair_12_00 — IRAM 0x2c=0x12, 0x2d=0x00 */

void stage_ctrl_pair_12_00(void)

{
  DAT_INTMEM_2c = 0x12;
  DAT_INTMEM_2d = 0;
  return;
}



/* ================================================================
 * FUN_CODE_1001 @ CODE:1001
 * ============================================================== */

void FUN_CODE_1001(void)

{
  IEPCNF0 = IEPCNF0 | 8;
  ep0_store_byte_and_arm_zlp(OEPCNF0 | 8);
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * timer0_tick_isr @ CODE:1016
 * ============================================================== */

/* timer0_tick_isr — TF0 ISR: set tick flag, reload TH0=0xCE */

void timer0_tick_isr(void)

{
  EA = 0;
  _4_0 = 1;
  TH0 = 0xce;
  EA = 1;
  return;
}



/* ================================================================
 * toggle_bit1E_state @ CODE:1020
 * ============================================================== */

/* toggle_bit1E_state — toggle bit 0x1e (0x23.6) */

void toggle_bit1E_state(void)

{
  if (_3_6 != '\0') {
    _3_6 = 0;
    return;
  }
  _3_6 = 1;
  return;
}



/* ================================================================
 * FUN_CODE_1029 @ CODE:1029
 * ============================================================== */

void FUN_CODE_1029(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_102a @ CODE:102a
 * ============================================================== */

void FUN_CODE_102a(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_102b @ CODE:102b
 * ============================================================== */

void FUN_CODE_102b(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_102c @ CODE:102c
 * ============================================================== */

void FUN_CODE_102c(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_102d @ CODE:102d
 * ============================================================== */

void FUN_CODE_102d(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_102e @ CODE:102e
 * ============================================================== */

void FUN_CODE_102e(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_102f @ CODE:102f
 * ============================================================== */

void FUN_CODE_102f(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1030 @ CODE:1030
 * ============================================================== */

void FUN_CODE_1030(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1031 @ CODE:1031
 * ============================================================== */

void FUN_CODE_1031(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1032 @ CODE:1032
 * ============================================================== */

void FUN_CODE_1032(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1033 @ CODE:1033
 * ============================================================== */

void FUN_CODE_1033(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1034 @ CODE:1034
 * ============================================================== */

void FUN_CODE_1034(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1035 @ CODE:1035
 * ============================================================== */

void FUN_CODE_1035(void)

{
  return;
}


