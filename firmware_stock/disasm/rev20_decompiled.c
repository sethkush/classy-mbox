
/* ================================================================
 * reset_vector @ CODE:0000
 * ============================================================== */

/* reset_vector — `LJMP 0x0A09` (C51 startup) */

void reset_vector(void)

{
  c51_startup();
  return;
}



/* ================================================================
 * int0_vector @ CODE:0003
 * ============================================================== */

/* int0_vector — `LJMP 0x0DAC` (USB ISR) */

void int0_vector(void)

{
  usb_int0_isr();
  return;
}



/* ================================================================
 * usb_ev_suspend @ CODE:0006
 * ============================================================== */

/* usb_ev_suspend — VECINT 0x16 SUSR handler: `IRAM 0x0A = 0x0E`, RET */

void usb_ev_suspend(void)

{
  pending_event = 0xe;
  return;
}



/* ================================================================
 * int1_body_reti @ CODE:000a
 * ============================================================== */

/* int1_body_reti — bare RETI (INT1 unused) */

void int1_body_reti(void)

{
  return;
}



/* ================================================================
 * timer0_vector @ CODE:000b
 * ============================================================== */

/* timer0_vector — `LJMP 0x101E` */

void timer0_vector(void)

{
  timer0_isr_tick();
  return;
}



/* ================================================================
 * timer1_body_reti @ CODE:000e
 * ============================================================== */

/* timer1_body_reti — bare RETI (Timer 1 unused) */

void timer1_body_reti(void)

{
  return;
}



/* ================================================================
 * uart_body_reti @ CODE:000f
 * ============================================================== */

/* uart_body_reti — bare RETI (UART unused) */

void uart_body_reti(void)

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
 * int1_vector @ CODE:0013
 * ============================================================== */

/* int1_vector — `LJMP 0x000A` */

void int1_vector(void)

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
 * timer1_vector @ CODE:001b
 * ============================================================== */

/* timer1_vector — `LJMP 0x000E` */

void timer1_vector(void)

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
 * uart_vector @ CODE:0023
 * ============================================================== */

/* uart_vector — `LJMP 0x000F` */

void uart_vector(void)

{
  return;
}



/* ================================================================
 * usb_ev_setup @ CODE:0026
 * ============================================================== */

/* WARNING: Control flow encountered bad instruction data */
/* usb_ev_setup — VECINT 0x12 SETUP dispatcher: clear stalls, set toggles, flush counts, branch on
   bmRequestType */

void usb_ev_setup(void)

{
  char cVar1;
  undefined1 *puVar2;
  
  ep0_clear_stall_both();
  IEPCNF0 = IEPCNF0 | 0x20;
  EP0_IN_remaining_length__low = ep0_store_cnf_and_arm_both(OEPCNF0 | 0x20);
  _1_5 = 0;
  EP0_IN_remaining_length__high = EP0_IN_remaining_length__low;
  puVar2 = (undefined1 *)0xff28;
  if (DAT_EXTMEM_ff28 == '\"') {
    pending_deferred_request = 1;
    _1_3 = 1;
    _1_4 = 0;
    return;
  }
  if (DAT_EXTMEM_ff28 == -0x5f) {
    ep0_ptr_set_in_buf();
    if (_5_4 == '\0') {
      dptr_from_ep0_ptr();
      *puVar2 = 1;
    }
    else {
      dptr_from_ep0_ptr();
      *puVar2 = 2;
    }
    ep0_send_1byte();
    return;
  }
  if (DAT_EXTMEM_ff28 == -0x5e) {
    puVar2 = (undefined1 *)0xff2b;
    if (DAT_EXTMEM_ff2b != '\x01') {
      ep0_stall_both();
      return;
    }
    ep0_ptr_set_in_buf();
    if (clock == '\x01') {
      dptr_from_ep0_ptr();
      *puVar2 = 0;
      BANK3_R4 = BANK3_R4 + '\x01';
      if (BANK3_R4 == '\0') {
        XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
      }
      ep0_buf_clear_byte();
      BANK3_R4 = BANK3_R4 + '\x01';
      if (BANK3_R4 == '\0') {
        XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
      }
      ep0_buf_clear_byte();
    }
    else if (clock == '\x02') {
      dptr_from_ep0_ptr();
      cVar1 = BANK3_R4;
      *puVar2 = 0x44;
      BANK3_R4 = BANK3_R4 + '\x01';
      if (BANK3_R4 == '\0') {
        XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
      }
      *(undefined1 *)CONCAT11(XDATA_destination_pointer__hi_lo,BANK3_R4) = 0xac;
      BANK3_R4 = cVar1 + '\x02';
      if (BANK3_R4 == '\0') {
        XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
      }
      ep0_buf_clear_byte();
    }
    else {
      if (clock != '\x03') {
        ep0_stall_both();
        return;
      }
      dptr_from_ep0_ptr();
      cVar1 = BANK3_R4;
      *puVar2 = 0x80;
      BANK3_R4 = BANK3_R4 + '\x01';
      if (BANK3_R4 == '\0') {
        XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
      }
      *(undefined1 *)CONCAT11(XDATA_destination_pointer__hi_lo,BANK3_R4) = 0xbb;
      BANK3_R4 = cVar1 + '\x02';
      if (BANK3_R4 == '\0') {
        XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
      }
      ep0_buf_clear_byte();
    }
    IEPDCNTX0 = 3;
    _1_3 = 0;
    _1_4 = 1;
    return;
  }
  if (DAT_EXTMEM_ff28 != '!') {
    switch_case_dispatch(DAT_EXTMEM_ff29);
                    /* WARNING: Bad instruction - Truncating control flow here */
    halt_baddata();
  }
  if (DAT_EXTMEM_ff29 == '\0') {
    pending_event = 0xd;
    _1_3 = 0;
    _1_4 = 0;
    return;
  }
  pending_deferred_request = 2;
  _1_3 = 1;
  _1_4 = 0;
  return;
}



/* ================================================================
 * setup_class_out_interface @ CODE:0055
 * ============================================================== */

/* setup_class_out_interface — bmReq 0x21: bRequest 0 \u2192 event 0x0D; else pending OUT cmd 2 */

void setup_class_out_interface(void)

{
  if (DAT_EXTMEM_ff29 == '\0') {
    pending_event = 0xd;
    _1_3 = 0;
    _1_4 = 0;
    return;
  }
  pending_deferred_request = 2;
  _1_3 = 1;
  _1_4 = 0;
  return;
}



/* ================================================================
 * setup_class_out_endpoint @ CODE:006b
 * ============================================================== */

/* setup_class_out_endpoint — bmReq 0x22 (UAC SET_CUR sample rate): pending OUT cmd 1 */

void setup_class_out_endpoint(void)

{
  pending_deferred_request = 1;
  _1_3 = 1;
  _1_4 = 0;
  return;
}



/* ================================================================
 * setup_get_input_source @ CODE:0073
 * ============================================================== */

/* setup_get_input_source — bmReq 0xA1: 1-byte reply 0x02/0x01 from bit 0x2C */

void setup_get_input_source(undefined1 *param_1)

{
  ep0_ptr_set_in_buf();
  if (_5_4 == '\0') {
    dptr_from_ep0_ptr();
    *param_1 = 1;
  }
  else {
    dptr_from_ep0_ptr();
    *param_1 = 2;
  }
  ep0_send_1byte();
  return;
}



/* ================================================================
 * setup_get_sample_freq @ CODE:008a
 * ============================================================== */

/* setup_get_sample_freq — bmReq 0xA2: 3-byte LE rate from IRAM 0x08, else stall */

void setup_get_sample_freq(void)

{
  char cVar1;
  undefined1 *puVar2;
  
  puVar2 = (undefined1 *)0xff2b;
  if (DAT_EXTMEM_ff2b != '\x01') {
    ep0_stall_both();
    return;
  }
  ep0_ptr_set_in_buf();
  if (clock == '\x01') {
    dptr_from_ep0_ptr();
    *puVar2 = 0;
    BANK3_R4 = BANK3_R4 + '\x01';
    if (BANK3_R4 == '\0') {
      XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
    }
    ep0_buf_clear_byte();
    BANK3_R4 = BANK3_R4 + '\x01';
    if (BANK3_R4 == '\0') {
      XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
    }
    ep0_buf_clear_byte();
  }
  else if (clock == '\x02') {
    dptr_from_ep0_ptr();
    cVar1 = BANK3_R4;
    *puVar2 = 0x44;
    BANK3_R4 = BANK3_R4 + '\x01';
    if (BANK3_R4 == '\0') {
      XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
    }
    *(undefined1 *)CONCAT11(XDATA_destination_pointer__hi_lo,BANK3_R4) = 0xac;
    BANK3_R4 = cVar1 + '\x02';
    if (BANK3_R4 == '\0') {
      XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
    }
    ep0_buf_clear_byte();
  }
  else {
    if (clock != '\x03') {
      ep0_stall_both();
      return;
    }
    dptr_from_ep0_ptr();
    cVar1 = BANK3_R4;
    *puVar2 = 0x80;
    BANK3_R4 = BANK3_R4 + '\x01';
    if (BANK3_R4 == '\0') {
      XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
    }
    *(undefined1 *)CONCAT11(XDATA_destination_pointer__hi_lo,BANK3_R4) = 0xbb;
    BANK3_R4 = cVar1 + '\x02';
    if (BANK3_R4 == '\0') {
      XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
    }
    ep0_buf_clear_byte();
  }
  IEPDCNTX0 = 3;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * send_3byte_ep0_reply @ CODE:010d
 * ============================================================== */

/* send_3byte_ep0_reply — `IEPDCNTX0 = 3`, clear 0x0B, set 0x0C */

void send_3byte_ep0_reply(void)

{
  IEPDCNTX0 = 3;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * std_request_dispatch @ CODE:0118
 * ============================================================== */

/* WARNING: Control flow encountered bad instruction data */
/* std_request_dispatch — A = bRequest, `LCALL 0x0F70` with inline table at 0x011F */

void std_request_dispatch(void)

{
  switch_case_dispatch(DAT_EXTMEM_ff29);
                    /* WARNING: Bad instruction - Truncating control flow here */
  halt_baddata();
}



/* ================================================================
 * std_clear_feature @ CODE:0144
 * ============================================================== */

/* std_clear_feature — bRequest 1; only bmReq 0x02 + wIndexL 0 accepted \u2192 un-stall EP0 */

void std_clear_feature(void)

{
  if ((DAT_EXTMEM_ff28 == '\x02') && (DAT_EXTMEM_ff2c == '\0')) {
    ep0_clear_stall_both();
    _1_3 = 0;
    _1_4 = 0;
    return;
  }
  ep0_stall_both();
  return;
}



/* ================================================================
 * std_get_configuration @ CODE:015d
 * ============================================================== */

/* std_get_configuration — bRequest 8; replies 1 or 0 from bit 0x0E */

void std_get_configuration(undefined1 *param_1)

{
  ep0_ptr_set_in_buf();
  if (_1_6 == '\0') {
    dptr_from_ep0_ptr();
    *param_1 = 0;
  }
  else {
    dptr_from_ep0_ptr();
    *param_1 = 1;
  }
  ep0_send_1byte();
  return;
}



/* ================================================================
 * std_get_descriptor @ CODE:0173
 * ============================================================== */

/* std_get_descriptor — bRequest 6; device/config/string selection, clamp, start IN transfer */

void std_get_descriptor(void)

{
  if (DAT_EXTMEM_ff2b == '\x01') {
    CODE_source_pointer__hi_lo = 5;
    BANK3_R2 = 0x96;
    EP0_IN_remaining_length__low = code_read_byte_at_srcptr();
    EP0_IN_remaining_length__high = 0;
  }
  else if ((DAT_EXTMEM_ff2b == '\x02') && (DAT_EXTMEM_ff2a == '\0')) {
    CODE_source_pointer__hi_lo = 6;
    BANK3_R2 = 0x70;
    EP0_IN_remaining_length__low = BYTE_ARRAY_CODE_0670[2];
    EP0_IN_remaining_length__high = BYTE_ARRAY_CODE_0670[3];
  }
  else {
    if (DAT_EXTMEM_ff2b != '\x03') {
      ep0_stall_both();
      return;
    }
    if (DAT_EXTMEM_ff2a == '\0') {
      CODE_source_pointer__hi_lo = 6;
      BANK3_R2 = 0xa6;
    }
    if (DAT_EXTMEM_ff2a == '\x01') {
      CODE_source_pointer__hi_lo = 6;
      BANK3_R2 = 0xaa;
    }
    if (DAT_EXTMEM_ff2a == '\x02') {
      CODE_source_pointer__hi_lo = 6;
      BANK3_R2 = 200;
    }
    EP0_IN_remaining_length__low = code_read_byte_at_srcptr();
    EP0_IN_remaining_length__high = 0;
  }
  ep0_clamp_len_to_wlength();
  ep0_in_start_transfer();
  return;
}



/* ================================================================
 * std_get_interface @ CODE:01f1
 * ============================================================== */

/* std_get_interface — bRequest 0x0A; 1-byte alt setting reply */

void std_get_interface(char param_1)

{
  char *pcVar1;
  undefined1 *puVar2;
  
  if ((_1_2 == '\x01') || (_1_6 != '\0')) {
    pcVar1 = &DAT_EXTMEM_ff2c;
    param_1 = DAT_EXTMEM_ff2c - 3;
    if (DAT_EXTMEM_ff2c < 3) {
      ep0_ptr_set_in_buf();
      if ((*pcVar1 == '\x01') && (_1_0 != '\0')) {
        dptr_from_ep0_ptr();
        *pcVar1 = '\x01';
      }
      else {
        puVar2 = (undefined1 *)0xff2c;
        if ((DAT_EXTMEM_ff2c == 2) && (_1_1 != '\0')) {
          dptr_from_ep0_ptr();
          *puVar2 = 2;
        }
        else {
          dptr_from_ep0_ptr();
          *puVar2 = 0;
        }
      }
      ep0_send_1byte();
      return;
    }
  }
  ep0_stall_both(param_1);
  return;
}



/* ================================================================
 * std_get_status @ CODE:022f
 * ============================================================== */

/* std_get_status — bRequest 0; always replies 0x0000 */

void std_get_status(undefined1 *param_1)

{
  ep0_ptr_set_in_buf();
  dptr_from_ep0_ptr();
  *param_1 = 0;
  BANK3_R4 = BANK3_R4 + '\x01';
  if (BANK3_R4 == '\0') {
    XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
  }
  ep0_buf_clear_byte();
  IEPDCNTX0 = 2;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * std_set_address @ CODE:024d
 * ============================================================== */

/* std_set_address — bRequest 5; defers address via IRAM 0x0D/0x0E */

void std_set_address(void)

{
  pending_deferred_request = 5;
  pending_USB_device_address = DAT_EXTMEM_ff2a;
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * std_set_configuration @ CODE:025b
 * ============================================================== */

/* std_set_configuration — bRequest 9; sets/clears bit 0x0E, queues event 1 */

void std_set_configuration(void)

{
  if (1 < DAT_EXTMEM_ff2a) {
    ep0_stall_both(DAT_EXTMEM_ff2a - 2);
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
  pending_event = 1;
  ep0_nack_both();
  return;
}



/* ================================================================
 * std_set_descriptor_stall @ CODE:0299
 * ============================================================== */

/* std_set_descriptor_stall — bRequest 7 \u2192 stall */

void std_set_descriptor_stall(void)

{
  ep0_stall_both();
  return;
}



/* ================================================================
 * std_set_feature_stall @ CODE:029c
 * ============================================================== */

/* std_set_feature_stall — bRequest 3 \u2192 stall */

void std_set_feature_stall(void)

{
  ep0_stall_both();
  return;
}



/* ================================================================
 * std_set_interface @ CODE:029f
 * ============================================================== */

/* std_set_interface — bRequest 0x0B; sets bit 0x08/0x09, queues event 2/3 */

void std_set_interface(void)

{
  char cVar1;
  
  cVar1 = DAT_EXTMEM_ff2c - 3;
  if (((DAT_EXTMEM_ff2c < 3) && (cVar1 = DAT_EXTMEM_ff2a - 2, DAT_EXTMEM_ff2a < 2)) &&
     ((_1_2 == '\x01' || (_1_6 != '\0')))) {
    if (DAT_EXTMEM_ff2c == 1) {
      _1_0 = DAT_EXTMEM_ff2a != 0;
      pending_event = 2;
    }
    else {
      if (DAT_EXTMEM_ff2c != 2) {
        ep0_stall_both();
        return;
      }
      _1_1 = DAT_EXTMEM_ff2a != 0;
      pending_event = 3;
    }
    ep0_nack_both();
    return;
  }
  ep0_stall_both(cVar1);
  return;
}



/* ================================================================
 * std_synch_frame_stall @ CODE:02e7
 * ============================================================== */

/* std_synch_frame_stall — bRequest 0x0C \u2192 stall */

void std_synch_frame_stall(void)

{
  ep0_stall_both();
  return;
}



/* ================================================================
 * std_request_unknown_default @ CODE:02ea
 * ============================================================== */

/* std_request_unknown_default — table default: stall + RET (no boot-ROM delegation) */

void std_request_unknown_default(void)

{
  ep0_stall_both();
  return;
}



/* ================================================================
 * device_event_dispatch @ CODE:02ee
 * ============================================================== */

/* device_event_dispatch — Dispatch IRAM 0x0A (1..14) through the table at 0x0300 */

void device_event_dispatch(void)

{
  if (0xd < pending_event - 1U) {
    pending_event = 0;
    return;
  }
                    /* WARNING: Could not recover jumptable at 0x02ff. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)((ushort)((pending_event - 1U) * '\x03') + 0x300))();
  return;
}



/* ================================================================
 * event_jump_table @ CODE:0300
 * ============================================================== */

/* event_jump_table — 14 \u00d7 `LJMP`, executed via `JMP @A+DPTR` */

void event_jump_table(void)

{
  byte bVar1;
  
  dma0_disable();
  DMACTL1 = DMACTL1 & 0x7f;
  GLOBCTL = GLOBCTL & 0xfe;
  if ((((_1_2 == '\x01') || (_1_6 != '\0')) && (_1_0 != '\x01')) && (_1_1 != '\x01')) {
    if (_1_2 != '\0') {
      codec_port_cfg3_commit(0xac,0xffde);
    }
    if (_1_6 != '\0') {
      codec_port_cfg3_commit(0xa8,0xffde);
    }
    if (_5_6 != '\x01') {
      audio_path_reconfig_ext_chips();
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
      shiftreg16_commit_p1_0_1_2();
    }
  }
  ep0_arm_zlp_in_and_out();
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd1_apply_clock_mode @ CODE:032a
 * ============================================================== */

/* cmd1_apply_clock_mode — Event 1: quiesce DMA + codec port, reprogram CPTCNF3/CPTRXCNF3,
   re-enable, hw init */

void cmd1_apply_clock_mode(void)

{
  byte bVar1;
  
  dma0_disable();
  DMACTL1 = DMACTL1 & 0x7f;
  GLOBCTL = GLOBCTL & 0xfe;
  if ((((_1_2 == '\x01') || (_1_6 != '\0')) && (_1_0 != '\x01')) && (_1_1 != '\x01')) {
    if (_1_2 != '\0') {
      codec_port_cfg3_commit(0xac,0xffde);
    }
    if (_1_6 != '\0') {
      codec_port_cfg3_commit(0xa8,0xffde);
    }
    if (_5_6 != '\x01') {
      audio_path_reconfig_ext_chips();
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
      shiftreg16_commit_p1_0_1_2();
    }
  }
  ep0_arm_zlp_in_and_out();
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd2_apply_iface1_alt @ CODE:0386
 * ============================================================== */

/* cmd2_apply_iface1_alt — Event 2: start/stop the IN-EP1 capture stream (and OUT EP2 when bit
   0x0E) */

void cmd2_apply_iface1_alt(void)

{
  if (((_1_2 == '\x01') || (_1_6 != '\0')) && (_1_0 != '\0')) {
    if (_5_6 != '\x01') {
      audio_path_reconfig_ext_chips();
    }
    _5_5 = 0;
    DAT_INTMEM_22 = 0xff;
    _2_0 = 0;
    _2_3 = 0;
    _3_6 = 0;
    _2_7 = 0;
    shiftreg8_commit_p1_7_6_5();
    _5_0 = 0;
    _5_1 = 0;
    _5_2 = 0;
    _5_3 = 0;
    _5_4 = 0;
    shiftreg16_commit_p1_0_1_2();
    IEPCNF1 = 0xc5;
    audio_clock_mode_apply(3);
    DMACTL1 = DMACTL1 | 0x80;
    if (_1_6 != '\0') {
      OEPCNF2 = 0xc5;
      DMACTL0 = DMACTL0 | 0x80;
    }
  }
  else if (((_1_2 == '\x01') || (_1_6 != '\0')) && (_1_0 != '\x01')) {
    DMACTL1 = DMACTL1 & 0x7f;
    _2_7 = 1;
    shiftreg8_commit_p1_7_6_5();
    if (_1_6 != '\0') {
      dma0_disable();
    }
  }
  USBIMSK = 0xff;
  ep0_arm_zlp_in_and_out();
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd3_apply_iface2_alt @ CODE:03fd
 * ============================================================== */

/* cmd3_apply_iface2_alt — Event 3: start/stop the OUT-EP2 playback stream */

void cmd3_apply_iface2_alt(void)

{
  if ((_1_2 == '\0' && _1_6 == '\0') || (_1_1 == '\0')) {
    if ((_1_2 != '\0' || _1_6 != '\0') && (_1_1 != '\x01')) {
      dma0_disable();
    }
  }
  else {
    if (_5_6 != '\x01') {
      audio_path_reconfig_ext_chips();
    }
    _5_5 = 0;
    OEPCNF2 = 0xc5;
    audio_clock_mode_apply(3);
    DMACTL0 = DMACTL0 | 0x80;
  }
  ep0_arm_zlp_in_and_out();
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd4_variantA_reapply_mode @ CODE:0454
 * ============================================================== */

/* cmd4_variantA_reapply_mode — Event 4: clear bit 0x2C, set bit 0x16, re-apply current mode */

void cmd4_variantA_reapply_mode(void)

{
  _5_4 = 0;
  _2_6 = 1;
  shiftreg16_commit_p1_0_1_2();
  shiftreg8_commit_p1_7_6_5();
  audio_clock_mode_apply(clock);
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd5_variantB_set_mode1 @ CODE:0466
 * ============================================================== */

/* cmd5_variantB_set_mode1 — Event 5: set bit 0x2C, clear bit 0x16, mode 1 */

void cmd5_variantB_set_mode1(void)

{
  _5_4 = 1;
  _2_6 = 0;
  shiftreg16_commit_p1_0_1_2();
  shiftreg8_commit_p1_7_6_5();
  audio_clock_mode_apply(1);
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd6_set_cpt_mode1 @ CODE:0478
 * ============================================================== */

/* cmd6_set_cpt_mode1 — Event 6: mode 1 */

void cmd6_set_cpt_mode1(void)

{
  audio_clock_mode_apply(1);
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd7_set_cpt_mode2_progchip @ CODE:0480
 * ============================================================== */

/* cmd7_set_cpt_mode2_progchip — Event 7: mode 2 + external-chip register set */

void cmd7_set_cpt_mode2_progchip(void)

{
  audio_clock_mode_apply(2);
  if (_5_4 == '\0') {
    DAT_INTMEM_2c = 0x23;
    DAT_INTMEM_2d = 0;
    serial_ctl_write_caller_pair_then_24_80();
  }
  else {
    serial_ctl_write_04_41_then_12_00();
  }
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd8_set_cpt_mode3_progchip @ CODE:049a
 * ============================================================== */

/* cmd8_set_cpt_mode3_progchip — Event 8: mode 3 + external-chip register set */

void cmd8_set_cpt_mode3_progchip(void)

{
  audio_clock_mode_apply(3);
  if (_5_4 == '\0') {
    DAT_INTMEM_2c = 0x23;
    DAT_INTMEM_2d = 0x40;
    serial_ctl_write_caller_pair_then_24_80();
  }
  else {
    serial_ctl_write_04_41_then_12_00();
  }
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd9_set_cpt_mode4 @ CODE:04b4
 * ============================================================== */

/* cmd9_set_cpt_mode4 — Event 9: mode 4 */

void cmd9_set_cpt_mode4(void)

{
  audio_clock_mode_apply(4);
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd10_set_cpt_mode5 @ CODE:04bc
 * ============================================================== */

/* cmd10_set_cpt_mode5 — Event 10: mode 5 */

void cmd10_set_cpt_mode5(void)

{
  audio_clock_mode_apply(5);
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd11_eeprom_selftest @ CODE:04c4
 * ============================================================== */

/* cmd11_eeprom_selftest — Event 11: complement/verify EEPROM byte 0x1FFF, report on bit 0x16 */

void cmd11_eeprom_selftest(void)

{
  byte bVar1;
  
  if (_5_6 != '\x01') {
    audio_path_reconfig_ext_chips();
  }
  _5_5 = 1;
  audio_clock_mode_apply(3);
  DAT_INTMEM_2c = 4;
  DAT_INTMEM_2d = 0x41;
  cs8427_ctl_write(0x41,4);
  bVar1 = 0x1f;
  i2c_eeprom_read_byte(0xff);
  DAT_INTMEM_2c = bVar1 ^ 0xff;
  bVar1 = 0x1f;
  FUN_CODE_0bee(DAT_INTMEM_2c);
  i2c_eeprom_read_byte(0xff);
  if (bVar1 == DAT_INTMEM_2c) {
    _2_6 = 0;
  }
  DAT_INTMEM_2d = bVar1;
  shiftreg8_commit_p1_7_6_5();
  DAT_INTMEM_2c = 0x12;
  DAT_INTMEM_2d = 0;
  cs8427_ctl_write(0,0x12);
  pending_event = 0;
  return;
}



/* ================================================================
 * cmd12_set_cpt_mode1 @ CODE:0511
 * ============================================================== */

/* cmd12_set_cpt_mode1 — Event 12: mode 1 (duplicate of cmd6) */

void cmd12_set_cpt_mode1(void)

{
  audio_clock_mode_apply(1);
  pending_event = 0;
  return;
}



/* ================================================================
 * evt0d_invalidate_boot_eeprom @ CODE:0518
 * ============================================================== */

/* evt0d_invalidate_boot_eeprom — Event 13: write 0x00 to boot-EEPROM offset 0, re-arm EP0 OUT */

void evt0d_invalidate_boot_eeprom(void)

{
  FUN_CODE_0bee(0,0,0);
  OEPDCNTX0 = 0;
  pending_event = 0;
  return;
}



/* ================================================================
 * evt0e_usb_suspend_enter_and_resume @ CODE:0526
 * ============================================================== */

/* evt0e_usb_suspend_enter_and_resume — Event 14: clocks off, latches idle, `PCON.IDL`; on resume
   disconnect/re-init/reconnect */

void evt0e_usb_suspend_enter_and_resume(void)

{
  byte bVar1;
  
  if ((char)((_1_6 & 1 | _1_2) << 7) < '\0') {
    ACGCTL = ACGCTL & 0x3f;
    latch_chain_byte_B = 0;
    latch_chain_byte_A = 0;
    shiftreg16_commit_p1_0_1_2();
    DAT_INTMEM_22 = 0xff;
    _3_6 = 0;
    shiftreg8_commit_p1_7_6_5();
    bVar1 = PCON;
    PCON = bVar1 | 1;
    USBCTL = USBCTL & 0x7f;
    USBIMSK = 0x9f;
    hw_master_init();
    usb_ep_dma_init();
    TR0 = 1;
    EX0 = 1;
    EA = 1;
    USBCTL = USBCTL | 0x80;
  }
  pending_event = 0;
  return;
}



/* ================================================================
 * evt_dispatch_epilogue @ CODE:0564
 * ============================================================== */

/* evt_dispatch_epilogue — `CLR A; MOV 0x0A,A; RET` */

void evt_dispatch_epilogue(void)

{
  pending_event = 0;
  return;
}



/* ================================================================
 * serial_ctl_write_04_41_then_12_00 @ CODE:0568
 * ============================================================== */

/* serial_ctl_write_04_41_then_12_00 — Ext-chip reg 0x04=0x41 then 0x12=0x00 */

void serial_ctl_write_04_41_then_12_00(void)

{
  DAT_INTMEM_2c = 4;
  DAT_INTMEM_2d = 0x41;
  cs8427_ctl_write(0x41,4);
  DAT_INTMEM_2c = 0x12;
  DAT_INTMEM_2d = 0;
  cs8427_ctl_write(0,0x12);
  return;
}



/* ================================================================
 * serial_ctl_write_caller_pair_then_24_80 @ CODE:0582
 * ============================================================== */

/* serial_ctl_write_caller_pair_then_24_80 — Ext-chip write (IRAM 0x2C,0x2D) then 0x24=0x80 */

void serial_ctl_write_caller_pair_then_24_80(void)

{
  cs8427_ctl_write(DAT_INTMEM_2d,DAT_INTMEM_2c);
  DAT_INTMEM_2c = 0x24;
  DAT_INTMEM_2d = 0x80;
  cs8427_ctl_write(0x80,0x24);
  return;
}



/* ================================================================
 * audio_clock_mode_apply @ CODE:0728
 * ============================================================== */

/* audio_clock_mode_apply — Mode 1/2/3/5: ACG synth + MCLK routing, ext-chip reg 4, iso EP re-arm,
   settle delay */

void audio_clock_mode_apply(char param_1)

{
  short sVar1;
  
  DAT_INTMEM_2f = 0;
  settle_delay_counter_low = 0;
  _3_2 = 0;
  _3_3 = 0;
  DAT_INTMEM_2e = param_1;
  shiftreg16_commit_p1_0_1_2();
  FUN_CODE_0e18(0xffe2);
  if (DAT_INTMEM_2e == '\x02') {
    ACGFRQ1 = 0x4b;
    ACGFRQ2 = 0x6a;
    ACGFRQ0 = 0x20;
    DMATSH3 = 0x4b;
    DMACTL3 = 0x6a;
    acg_commit_and_ctl(0x20,0xfff9);
    clock = 2;
    queue_cs8427_reg4_val40();
  }
  else if (DAT_INTMEM_2e == '\x03') {
    acg_set_freq_48k_family();
    clock = 3;
    queue_cs8427_reg4_val40();
  }
  else if (DAT_INTMEM_2e == '\x05') {
    GLOBCTL = GLOBCTL & 0xfe;
    CPTRXCNF4 = 1;
    sVar1 = -0x4f;
    sfr_store_then_cpt_cfg_tail(GLOBCTL | 1);
    *(undefined1 *)(sVar1 + 1) = 0;
    DMATSL2 = 0x10;
    _3_0 = 1;
    _3_1 = 1;
    shiftreg16_commit_p1_0_1_2();
    clock = 5;
    queue_cs8427_reg4_val40();
  }
  else if (DAT_INTMEM_2e == '\x01') {
    ACGCTL = 0xd;
    clock = 1;
    DAT_INTMEM_31 = 4;
    DAT_INTMEM_32 = 0x41;
  }
  cs8427_ctl_write(DAT_INTMEM_32,DAT_INTMEM_31);
  ACGCTL = ACGCTL | 0xc0;
  IEPDCNTX1 = 0;
  IEPDCNTY1 = 0;
  OEPDCNTX2 = 0;
  OEPDCNTY2 = 0;
  IEPCNF1 = 0xc5;
  OEPCNF2 = 0xc5;
  _3_2 = 1;
  _3_3 = 1;
  shiftreg16_commit_p1_0_1_2();
  DAT_INTMEM_2f = '\0';
  settle_delay_counter_low = '\0';
  do {
    settle_delay_counter_low = settle_delay_counter_low + '\x01';
    if (settle_delay_counter_low == '\0') {
      DAT_INTMEM_2f = DAT_INTMEM_2f + '\x01';
    }
  } while ((settle_delay_counter_low != -1) || (DAT_INTMEM_2f != '\x0f'));
  return;
}



/* ================================================================
 * audio_path_reconfig_ext_chips @ CODE:080b
 * ============================================================== */

/* audio_path_reconfig_ext_chips — Latch reset, MCLK enable, CS pulse, 10 external-chip register
   writes; sets bit 0x2E */

void audio_path_reconfig_ext_chips(void)

{
  latch_chain_byte_B = 0;
  latch_chain_byte_A = 0;
  _5_6 = 1;
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  shiftreg16_commit_p1_0_1_2();
  acg_set_freq_48k_family();
  FUN_CODE_0e17();
  clock = 3;
  ACGCTL = ACGCTL | 0xc0;
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  _3_2 = 1;
  _3_3 = 1;
  shiftreg16_commit_p1_0_1_2();
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  _5_7 = 1;
  _3_4 = 1;
  shiftreg16_commit_p1_0_1_2();
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  _5_7 = 0;
  shiftreg16_commit_p1_0_1_2();
  _5_7 = 1;
  shiftreg16_commit_p1_0_1_2();
  extchip_write_reg4_zero();
  DAT_INTMEM_2e = 0x13;
  DAT_INTMEM_2f = 0x10;
  extchip_write_2e_2f();
  extchip_write_reg4_zero();
  DAT_INTMEM_2e = 4;
  DAT_INTMEM_2f = 0x40;
  extchip_write_2e_2f();
  DAT_INTMEM_2e = 1;
  DAT_INTMEM_2f = 1;
  extchip_write_2e_2f_dup();
  DAT_INTMEM_2e = 2;
  DAT_INTMEM_2f = 0x20;
  extchip_write_2e_2f_dup();
  DAT_INTMEM_2e = 3;
  DAT_INTMEM_2f = 0xc;
  cs8427_ctl_write(0xc,3);
  DAT_INTMEM_2e = 5;
  extchip_write_val05();
  DAT_INTMEM_2e = 6;
  extchip_write_val05();
  DAT_INTMEM_2e = 0x11;
  DAT_INTMEM_2f = 0xff;
  cs8427_ctl_write(0xff,0x11);
  return;
}



/* ================================================================
 * extchip_write_reg4_zero @ CODE:08a6
 * ============================================================== */

/* extchip_write_reg4_zero — reg 0x04 = 0x00 */

void extchip_write_reg4_zero(void)

{
  DAT_INTMEM_2e = 4;
  DAT_INTMEM_2f = 0;
  cs8427_ctl_write(0,4);
  return;
}



/* ================================================================
 * extchip_write_val05 @ CODE:08b3
 * ============================================================== */

/* extchip_write_val05 — reg IRAM[0x2E] = 0x05 */

void extchip_write_val05(void)

{
  DAT_INTMEM_2f = 5;
  cs8427_ctl_write(5,DAT_INTMEM_2e);
  return;
}



/* ================================================================
 * extchip_write_2e_2f @ CODE:08bd
 * ============================================================== */

/* extchip_write_2e_2f — reg IRAM[0x2E] = IRAM[0x2F] */

void extchip_write_2e_2f(void)

{
  cs8427_ctl_write(DAT_INTMEM_2f,DAT_INTMEM_2e);
  return;
}



/* ================================================================
 * extchip_write_2e_2f_dup @ CODE:08c4
 * ============================================================== */

/* extchip_write_2e_2f_dup — byte-identical duplicate of 0x08BD */

void extchip_write_2e_2f_dup(void)

{
  cs8427_ctl_write(DAT_INTMEM_2f,DAT_INTMEM_2e);
  return;
}



/* ================================================================
 * hw_master_init @ CODE:08cb
 * ============================================================== */

/* hw_master_init — USBCTL=0, MEMCFG.SDW, ports, timers, IE/IP, GLOBCTL, full codec-port I\u00b2S
   mode 5, ACG, latches, settle */

void hw_master_init(void)

{
  byte bVar1;
  
  DAT_INTMEM_2e = 0;
  DAT_INTMEM_2f = 0;
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
  sfr_store_then_cpt_cfg_tail(3,0xffd4);
  FUN_CODE_0e17();
  GLOBCTL = GLOBCTL | 1;
  clock = 3;
  DAT_INTMEM_22 = 0;
  _3_6 = 1;
  shiftreg8_commit_p1_7_6_5();
  while( true ) {
    bVar1 = ~DAT_INTMEM_2f;
    if (bVar1 == 0) {
      bVar1 = DAT_INTMEM_2e ^ 0xf;
    }
    if (bVar1 == 0) break;
    DAT_INTMEM_2f = DAT_INTMEM_2f + 1;
    if (DAT_INTMEM_2f == 0) {
      DAT_INTMEM_2e = DAT_INTMEM_2e + 1;
    }
  }
  DAT_INTMEM_22 = 0xff;
  _2_0 = 0;
  _2_3 = 0;
  _3_6 = 0;
  shiftreg8_commit_p1_7_6_5();
  latch_chain_byte_B = 0;
  latch_chain_byte_A = 0;
  shiftreg16_commit_p1_0_1_2();
  return;
}



/* ================================================================
 * usb_ep_dma_init @ CODE:0970
 * ============================================================== */

/* usb_ep_dma_init — EP0/EP1/EP2 buffers + configs, DMA ch0/ch1, USBIMSK=0x9F, USBFADR=0, clear
   EP0 state */

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
  EP0_IN_remaining_length__low = 0;
  EP0_IN_remaining_length__high = 0;
  UNKNOWN = 0xfe;
  pending_event = 0;
  return;
}



/* ================================================================
 * c51_startup @ CODE:0a09
 * ============================================================== */

/* c51_startup — Clear IRAM 0x01-0x7F, SP=0x33, jump to init interpreter */

void c51_startup(byte param_1)

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
  SP = 0x33;
  pbVar7 = BYTE_ARRAY_CODE_0f9c;
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
      param_1 = 0;
    }
    else if (cVar1 < '\0') {
      do {
        bVar3 = *pbVar7;
        pbVar7 = pbVar7 + 1;
        pbVar4 = (byte *)((bVar3 & 0x7f) >> 3 | 0x20);
        bVar5 = *(byte *)((ushort)((bVar3 & 7) + 0xc) + 0xa3c);
        if ((char)bVar3 < '\0') {
          bVar5 = bVar5 | *pbVar4;
        }
        else {
          bVar5 = ~bVar5 & *pbVar4;
        }
        *pbVar4 = bVar5;
        bVar6 = bVar6 - 1;
      } while (bVar6 != 0);
      param_1 = 0;
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
      param_1 = 0;
    }
  }
  P3_1_edge_state = '\0';
  startup_delay_counter = 0xff;
  DAT_INTMEM_29 = -1;
  UNKNOWN = 0;
  UNKNOWN = 0x10;
  EA = 0;
  USBIMSK = 0;
  _4_2 = 0;
  hw_master_init();
  usb_ep_dma_init();
  while (cVar1 = DAT_INTMEM_29,
        (byte)-(((DAT_INTMEM_29 == '\0') << 7) >> 7) <= startup_delay_counter) {
    DAT_INTMEM_29 = DAT_INTMEM_29 + -1;
    if (cVar1 == '\0') {
      startup_delay_counter = startup_delay_counter - 1;
    }
  }
  TR0 = 1;
  EA = 1;
  USBCTL = USBCTL | 0x80;
  do {
    while (_4_0 != '\x01') {
      if (pending_event != '\0') {
        device_event_dispatch();
      }
    }
    p3_button_scan();
    if ((param_1 & 1) != 0) {
      shiftreg8_commit_p1_7_6_5();
      shiftreg16_commit_p1_0_1_2();
    }
    if ((_0_1 != '\x01') && (P3_1_edge_state == '\0')) {
      P3_1_edge_state = '\x01';
      pending_event = '\v';
      device_event_dispatch();
    }
    if ((_0_1 != '\0') && (P3_1_edge_state == '\x01')) {
      P3_1_edge_state = '\0';
      pending_event = '\f';
      device_event_dispatch();
    }
    _4_0 = '\0';
  } while( true );
}



/* ================================================================
 * c51_init_interpreter @ CODE:0a18
 * ============================================================== */

/* c51_init_interpreter — Walks the record table at 0x0F9C (IDATA / PDATA / XDATA / bit records)
    */

void c51_init_interpreter(byte *param_1,byte param_2)

{
  char cVar1;
  byte *pbVar2;
  byte bVar3;
  byte bVar4;
  byte bVar5;
  char in_PSW;
  byte *pbVar6;
  
  do {
    pbVar2 = (byte *)*param_1;
    param_1 = param_1 + 1;
    do {
      bVar3 = *param_1;
      param_1 = param_1 + 1;
      if (in_PSW < '\0') {
        *(byte *)ZEXT12(pbVar2) = bVar3;
      }
      else {
        *pbVar2 = bVar3;
      }
      pbVar2 = pbVar2 + '\x01';
      param_2 = param_2 - 1;
    } while (param_2 != 0);
    while( true ) {
      bVar5 = 0;
      bVar3 = 1;
      bVar4 = *param_1;
      if (bVar4 == 0) {
        P3_1_edge_state = '\0';
        startup_delay_counter = 0xff;
        DAT_INTMEM_29 = -1;
        UNKNOWN = 0;
        UNKNOWN = 0x10;
        EA = 0;
        USBIMSK = 0;
        _4_2 = 0;
        hw_master_init();
        usb_ep_dma_init();
        while (cVar1 = DAT_INTMEM_29,
              (byte)-(((DAT_INTMEM_29 == '\0') << 7) >> 7) <= startup_delay_counter) {
          DAT_INTMEM_29 = DAT_INTMEM_29 + -1;
          if (cVar1 == '\0') {
            startup_delay_counter = startup_delay_counter - 1;
          }
        }
        TR0 = 1;
        EA = 1;
        USBCTL = USBCTL | 0x80;
        do {
          while (_4_0 != '\x01') {
            if (pending_event != '\0') {
              device_event_dispatch();
            }
          }
          p3_button_scan();
          if ((bVar5 & 1) != 0) {
            shiftreg8_commit_p1_7_6_5();
            shiftreg16_commit_p1_0_1_2();
          }
          if ((_0_1 != '\x01') && (P3_1_edge_state == '\0')) {
            P3_1_edge_state = '\x01';
            pending_event = '\v';
            device_event_dispatch();
          }
          if ((_0_1 != '\0') && (P3_1_edge_state == '\x01')) {
            P3_1_edge_state = '\0';
            pending_event = '\f';
            device_event_dispatch();
          }
          _4_0 = '\0';
        } while( true );
      }
      param_2 = bVar4 & 0x3f;
      pbVar6 = param_1 + 1;
      if (param_2 >> 5 != 0) {
        bVar3 = bVar4 & 0x1f;
        param_2 = param_1[1];
        pbVar6 = param_1 + 2;
        if (param_2 != 0) {
          bVar3 = bVar3 + 1;
        }
      }
      param_1 = pbVar6;
      in_PSW = CARRY1(bVar4 & 0xc0,bVar4 & 0xc0) << 7;
      if ((bVar4 & 0x40) == 0) break;
      if (in_PSW < '\0') {
        do {
          bVar3 = *param_1;
          param_1 = param_1 + 1;
          pbVar2 = (byte *)((bVar3 & 0x7f) >> 3 | 0x20);
          bVar5 = *(byte *)((ushort)((bVar3 & 7) + 0xc) + 0xa3c);
          if ((char)bVar3 < '\0') {
            bVar5 = bVar5 | *pbVar2;
          }
          else {
            bVar5 = ~bVar5 & *pbVar2;
          }
          *pbVar2 = bVar5;
          param_2 = param_2 - 1;
        } while (param_2 != 0);
      }
      else {
        pbVar6 = *(byte **)param_1;
        param_1 = param_1 + 2;
        do {
          do {
            bVar5 = *param_1;
            param_1 = param_1 + 1;
            *pbVar6 = bVar5;
            pbVar6 = pbVar6 + 1;
            param_2 = param_2 - 1;
          } while (param_2 != 0);
          bVar3 = bVar3 - 1;
        } while (bVar3 != 0);
      }
    }
  } while( true );
}



/* ================================================================
 * main @ CODE:0a95
 * ============================================================== */

/* main — Init, USB attach, forever loop (event drain + 1 ms tick work) */

void main(byte param_1)

{
  char cVar1;
  
  P3_1_edge_state = '\0';
  startup_delay_counter = 0xff;
  DAT_INTMEM_29 = -1;
  UNKNOWN = 0;
  UNKNOWN = 0x10;
  EA = 0;
  USBIMSK = 0;
  _4_2 = 0;
  hw_master_init();
  usb_ep_dma_init();
  while (cVar1 = DAT_INTMEM_29,
        (byte)-(((DAT_INTMEM_29 == '\0') << 7) >> 7) <= startup_delay_counter) {
    DAT_INTMEM_29 = DAT_INTMEM_29 + -1;
    if (cVar1 == '\0') {
      startup_delay_counter = startup_delay_counter - 1;
    }
  }
  TR0 = 1;
  EA = 1;
  USBCTL = USBCTL | 0x80;
  do {
    while (_4_0 != '\x01') {
      if (pending_event != '\0') {
        device_event_dispatch();
      }
    }
    p3_button_scan();
    if ((param_1 & 1) != 0) {
      shiftreg8_commit_p1_7_6_5();
      shiftreg16_commit_p1_0_1_2();
    }
    if ((_0_1 != '\x01') && (P3_1_edge_state == '\0')) {
      P3_1_edge_state = '\x01';
      pending_event = '\v';
      device_event_dispatch();
    }
    if ((_0_1 != '\0') && (P3_1_edge_state == '\x01')) {
      P3_1_edge_state = '\0';
      pending_event = '\f';
      device_event_dispatch();
    }
    _4_0 = '\0';
  } while( true );
}



/* ================================================================
 * FUN_CODE_0b11 @ CODE:0b11
 * ============================================================== */

void FUN_CODE_0b11(void)

{
  XDATA_destination_pointer__hi_lo = 0xfa;
  BANK3_R4 = 0x10;
  return;
}



/* ================================================================
 * dptr_from_ep0_ptr @ CODE:0b17
 * ============================================================== */

/* dptr_from_ep0_ptr — DPTR = 0x1B:0x1C */

void dptr_from_ep0_ptr(void)

{
  return;
}



/* ================================================================
 * ep0_clear_stall_toggle_and_arm @ CODE:0b1e
 * ============================================================== */

/* ep0_clear_stall_toggle_and_arm — IEPCNF0/OEPCNF0 &= 0xD7, then falls into 0x0B2B */

void ep0_clear_stall_toggle_and_arm(void)

{
  IEPCNF0 = IEPCNF0 & 0xd7;
  OEPCNF0 = OEPCNF0 & 0xd7;
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * ep0_store_cnf_and_arm_both @ CODE:0b2b
 * ============================================================== */

/* ep0_store_cnf_and_arm_both — Store caller's A to caller's DPTR; IEPDCNTX0=0, OEPDCNTX0=0 */

void ep0_store_cnf_and_arm_both(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * ep0_buf_clear_byte @ CODE:0b36
 * ============================================================== */

/* ep0_buf_clear_byte — XDATA[0x1B:A] = 0 */

void ep0_buf_clear_byte(undefined1 param_1)

{
  *(undefined1 *)CONCAT11(XDATA_destination_pointer__hi_lo,param_1) = 0;
  return;
}



/* ================================================================
 * ep0_ptr_set_in_buf @ CODE:0b3e
 * ============================================================== */

/* ep0_ptr_set_in_buf — 0x1B:0x1C = 0xFA18 (EP0 IN buffer) */

void ep0_ptr_set_in_buf(void)

{
  XDATA_destination_pointer__hi_lo = 0xfa;
  BANK3_R4 = 0x18;
  return;
}



/* ================================================================
 * ep0_send_1byte @ CODE:0b45
 * ============================================================== */

/* ep0_send_1byte — `IEPDCNTX0 = 1`; clear 0x0B, set 0x0C */

void ep0_send_1byte(void)

{
  IEPDCNTX0 = 1;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * ep0_clear_stall_both @ CODE:0b50
 * ============================================================== */

/* ep0_clear_stall_both — IEPCNF0/OEPCNF0 &= 0xF7 */

void ep0_clear_stall_both(void)

{
  IEPCNF0 = IEPCNF0 & 0xf7;
  OEPCNF0 = OEPCNF0 & 0xf7;
  return;
}



/* ================================================================
 * ep0_nack_both @ CODE:0b5f
 * ============================================================== */

/* ep0_nack_both — IEPDCNTX0 = OEPDCNTX0 = 0x80; clear 0x0B/0x0C */

void ep0_nack_both(void)

{
  IEPDCNTX0 = 0x80;
  OEPDCNTX0 = 0x80;
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * code_read_byte_at_srcptr @ CODE:0b6e
 * ============================================================== */

/* code_read_byte_at_srcptr — A = CODE[0x19:0x1A] */

undefined1 code_read_byte_at_srcptr(void)

{
  return *(undefined1 *)CONCAT11(CODE_source_pointer__hi_lo,BANK3_R2);
}



/* ================================================================
 * ep0_in_start_transfer @ CODE:0b77
 * ============================================================== */

/* ep0_in_start_transfer — Fill chunk then clear the NACK bit to release the packet */

void ep0_in_start_transfer(void)

{
  ep0_in_fill_chunk();
  IEPDCNTX0 = IEPDCNTX0 & 0x7f;
  return;
}



/* ================================================================
 * ep0_arm_zlp_and_out @ CODE:0b82
 * ============================================================== */

/* ep0_arm_zlp_and_out — OEPDCNTX0 = 0, IEPDCNTX0 = 0 */

void ep0_arm_zlp_and_out(void)

{
  OEPDCNTX0 = 0;
  IEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * ep0_in_fill_chunk @ CODE:0b8c
 * ============================================================== */

/* ep0_in_fill_chunk — Copy \u22648 bytes CODE\u2192EP0 IN buffer, update counts and state bits */

void ep0_in_fill_chunk(undefined1 *param_1)

{
  undefined1 uVar1;
  
  EP0_IN_packet_fill_counter = 0;
  ep0_ptr_set_in_buf();
  do {
    if ((EP0_IN_remaining_length__low == '\0') << 7 < '\0') break;
    code_read_byte_at_srcptr(EP0_IN_remaining_length__low + -1);
    uVar1 = dptr_from_ep0_ptr();
    *param_1 = uVar1;
    BANK3_R4 = BANK3_R4 + '\x01';
    if (BANK3_R4 == '\0') {
      XDATA_destination_pointer__hi_lo = XDATA_destination_pointer__hi_lo + '\x01';
    }
    BANK3_R2 = BANK3_R2 + '\x01';
    if (BANK3_R2 == '\0') {
      CODE_source_pointer__hi_lo = CODE_source_pointer__hi_lo + '\x01';
    }
    EP0_IN_remaining_length__low = EP0_IN_remaining_length__low + -1;
    if ((EP0_IN_remaining_length__low == '\0') && (EP0_IN_remaining_length__high != '\0')) {
      EP0_IN_remaining_length__low = -1;
      EP0_IN_remaining_length__high = EP0_IN_remaining_length__high + -1;
    }
    EP0_IN_packet_fill_counter = EP0_IN_packet_fill_counter + 1;
  } while (EP0_IN_packet_fill_counter != 8);
  IEPDCNTX0 = EP0_IN_packet_fill_counter | 0x80;
  _1_3 = 1;
  _1_4 = 0;
  if ((EP0_IN_remaining_length__low == '\0') && (EP0_IN_remaining_length__high == '\0')) {
    if ((EP0_IN_packet_fill_counter == 8) && (_1_5 != '\0')) {
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
 * FUN_CODE_0bee @ CODE:0bee
 * ============================================================== */

void FUN_CODE_0bee(undefined1 param_1)

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
 * cs8427_ctl_write @ CODE:0c45
 * ============================================================== */

void cs8427_ctl_write(undefined1 param_1)

{
  char cVar1;
  char cVar2;
  char cVar3;
  byte bVar4;
  
  cVar3 = '\b';
  cVar2 = '\x01';
  _5_7 = 0;
  DAT_INTMEM_33 = param_1;
  shiftreg16_commit_p1_0_1_2(BANK0_R5,0x20);
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
  shiftreg16_commit_p1_0_1_2();
  return;
}



/* ================================================================
 * i2c_eeprom_read_byte @ CODE:0cdd
 * ============================================================== */

undefined1 i2c_eeprom_read_byte(void)

{
  do {
  } while ((I2CSTA >> 3 & 1) == 0);
  do {
  } while ((I2CSTA >> 3 & 1) == 0);
  scan_event_flags = scan_event_flags | 1;
  I2CADR = 0xa0;
  I2CDATO = 0;
  I2CSTA = I2CSTA & 0xfc | 2;
  do {
  } while (-1 < (char)I2CSTA);
  return I2CDATI;
}



/* ================================================================
 * ep0_out_data_handler @ CODE:0d25
 * ============================================================== */

void ep0_out_data_handler(char *param_1)

{
  char cVar1;
  
  if (_1_3 != '\0') {
    if (pending_deferred_request == '\x01') {
      FUN_CODE_0b11();
      cVar1 = *param_1;
      if (cVar1 == 'D') {
        pending_event = 7;
      }
      if (cVar1 == -0x80) {
        pending_event = 8;
      }
      if (cVar1 == '\0') {
        pending_event = 6;
      }
    }
    if (pending_deferred_request == '\x02') {
      FUN_CODE_0b11();
      if (*param_1 == '\x01') {
        pending_event = 4;
      }
      else {
        pending_event = 5;
      }
    }
    _1_3 = 0;
    _1_4 = 0;
    IEPCNF0 = IEPCNF0 | 0x20;
    ep0_arm_zlp_and_out();
    return;
  }
  ep0_clear_stall_toggle_and_arm();
  return;
}



/* ================================================================
 * ep0_clamp_len_to_wlength @ CODE:0d6b
 * ============================================================== */

byte ep0_clamp_len_to_wlength(void)

{
  byte bVar1;
  
  if ((DAT_EXTMEM_ff2f + 1 <= EP0_IN_remaining_length__high) ||
     ((DAT_EXTMEM_ff2f == EP0_IN_remaining_length__high &&
      (DAT_EXTMEM_ff2e + 1 <= EP0_IN_remaining_length__low)))) {
    EP0_IN_remaining_length__low = DAT_EXTMEM_ff2e;
    EP0_IN_remaining_length__high = DAT_EXTMEM_ff2f;
    _1_5 = 0;
  }
  bVar1 = EP0_IN_remaining_length__low - DAT_EXTMEM_ff2e;
  if (((EP0_IN_remaining_length__low < DAT_EXTMEM_ff2e) << 7 < '\0') ||
     ((bVar1 = DAT_EXTMEM_ff2e, DAT_EXTMEM_ff2e == EP0_IN_remaining_length__low &&
      (bVar1 = EP0_IN_remaining_length__high - DAT_EXTMEM_ff2f,
      EP0_IN_remaining_length__high < DAT_EXTMEM_ff2f)))) {
    _1_5 = 1;
  }
  return bVar1;
}



/* ================================================================
 * usb_int0_isr @ CODE:0dac
 * ============================================================== */

undefined1 usb_int0_isr(undefined1 param_1)

{
  undefined1 *puVar1;
  
  EA = 0;
  puVar1 = (undefined1 *)
           CONCAT11('\f' - (((0x6cU < (byte)(VECINT * '\x02')) << 7) >> 7),VECINT * '\x02' + 0x93);
  jmp_r2r1_trampoline(puVar1[1],BANK2_R6,0xff,*puVar1,VECINT);
  VECINT = 0;
  EA = 1;
  return param_1;
}



/* ================================================================
 * sfr_store_then_cpt_cfg_tail @ CODE:0deb
 * ============================================================== */

void sfr_store_then_cpt_cfg_tail(undefined1 param_1,undefined1 *param_2)

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
 * acg_set_freq_48k_family @ CODE:0dec
 * ============================================================== */

void acg_set_freq_48k_family(void)

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
 * acg_commit_and_ctl @ CODE:0e0f
 * ============================================================== */

void acg_commit_and_ctl(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  ACGCTL = 6;
  return;
}



/* ================================================================
 * FUN_CODE_0e17 @ CODE:0e17
 * ============================================================== */

void FUN_CODE_0e17(short param_1)

{
  *(undefined1 *)(param_1 + 1) = 0x10;
  DMATSL2 = 0x10;
  return;
}



/* ================================================================
 * FUN_CODE_0e18 @ CODE:0e18
 * ============================================================== */

void FUN_CODE_0e18(undefined1 *param_1)

{
  *param_1 = 0x10;
  DMATSL2 = 0x10;
  return;
}



/* ================================================================
 * queue_cs8427_reg4_val40 @ CODE:0e20
 * ============================================================== */

void queue_cs8427_reg4_val40(void)

{
  DAT_INTMEM_31 = 4;
  DAT_INTMEM_32 = 0x40;
  return;
}



/* ================================================================
 * button_a_cycle_3state @ CODE:0e27
 * ============================================================== */

void button_a_cycle_3state(void)

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
 * shiftreg16_commit_p1_0_1_2 @ CODE:0e62
 * ============================================================== */

void shiftreg16_commit_p1_0_1_2(void)

{
  bool bVar1;
  char cVar2;
  char cVar3;
  byte bVar4;
  
  cVar3 = '\b';
  _6_0 = 1;
  bVar1 = true;
  while( true ) {
    for (; cVar3 != '\0'; cVar3 = cVar3 + -1) {
      cVar2 = '\x02';
      bVar4 = BANK0_R5;
      while (cVar2 = cVar2 + -1, cVar2 != '\0') {
        bVar4 = bVar4 << 1 | bVar4 >> 7;
      }
      if ((bVar4 & 1) == 0) {
        bVar4 = P1;
        P1 = bVar4 & 0xfe;
      }
      else {
        bVar4 = P1;
        P1 = bVar4 | 1;
      }
      bVar4 = P1;
      P1 = bVar4 | 4;
      bVar4 = P1;
      P1 = bVar4 & 0xfb;
    }
    if (!bVar1) break;
    _6_0 = 0;
    bVar1 = false;
    cVar3 = '\b';
  }
  bVar4 = P1;
  P1 = bVar4 | 2;
  bVar4 = P1;
  P1 = bVar4 & 0xfd;
  return;
}



/* ================================================================
 * button_b_cycle_3state @ CODE:0e9d
 * ============================================================== */

void button_b_cycle_3state(void)

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
 * p3_button_scan @ CODE:0ed5
 * ============================================================== */

void p3_button_scan(void)

{
  byte bVar1;
  
  bVar1 = P3;
  if (bVar1 == previous_P3_snapshot) {
    return;
  }
  if ((_0_5 != '\x01') && ((bVar1 >> 5 & 1) != 0)) {
    toggle_flag_bit1e(0);
    scan_event_flags = scan_event_flags | 1;
  }
  if ((_0_3 != '\x01') && ((bVar1 >> 3 & 1) != 0)) {
    button_a_cycle_3state();
    scan_event_flags = scan_event_flags | 1;
  }
  if ((_0_4 != '\x01') && ((bVar1 >> 4 & 1) != 0)) {
    button_b_cycle_3state();
    scan_event_flags = scan_event_flags | 1;
  }
  previous_P3_snapshot = bVar1;
  return;
}



/* ================================================================
 * shiftreg8_commit_p1_7_6_5 @ CODE:0f0c
 * ============================================================== */

void shiftreg8_commit_p1_7_6_5(void)

{
  char cVar1;
  char cVar2;
  byte bVar3;
  
  cVar2 = '\b';
  bVar3 = P1;
  P1 = bVar3 & 0xbf;
  do {
    cVar1 = '\x02';
    bVar3 = BANK0_R5;
    while (cVar1 = cVar1 + -1, cVar1 != '\0') {
      bVar3 = bVar3 << 1 | bVar3 >> 7;
    }
    if ((bVar3 & 1) == 0) {
      bVar3 = P1;
      P1 = bVar3 & 0x7f;
    }
    else {
      bVar3 = P1;
      P1 = bVar3 | 0x80;
    }
    bVar3 = P1;
    P1 = bVar3 | 0x20;
    bVar3 = P1;
    P1 = bVar3 & 0xdf;
    cVar2 = cVar2 + -1;
  } while (cVar2 != '\0');
  if (_3_6 != '\0') {
    bVar3 = P1;
    P1 = bVar3 | 0xc0;
    return;
  }
  bVar3 = P1;
  P1 = bVar3 & 0x7f;
  bVar3 = P1;
  P1 = bVar3 | 0x40;
  bVar3 = P1;
  P1 = bVar3 & 0xbf;
  return;
}



/* ================================================================
 * usb_rstr_handler @ CODE:0f43
 * ============================================================== */

void usb_rstr_handler(void)

{
  OEPDCNTX2 = ep0_arm_zlp_and_out();
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
 * switch_case_dispatch @ CODE:0f70
 * ============================================================== */

void switch_case_dispatch(char param_1)

{
  char *pcVar1;
  undefined1 uStackX_0;
  undefined1 in_stack_000000ff;
  
  for (pcVar1 = (char *)CONCAT11(uStackX_0,in_stack_000000ff);
      (*pcVar1 != '\0' || (pcVar1[1] != '\0')); pcVar1 = pcVar1 + 3) {
    if (pcVar1[2] == param_1) goto LAB_CODE_0f80;
  }
  pcVar1 = pcVar1 + 2;
LAB_CODE_0f80:
                    /* WARNING: Could not recover jumptable at 0x0f8a. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (**(code **)pcVar1)();
  return;
}



/* ================================================================
 * jmp_r2r1_trampoline @ CODE:0f96
 * ============================================================== */

void jmp_r2r1_trampoline(undefined1 param_1,undefined1 param_2)

{
                    /* WARNING: Could not recover jumptable at 0x0f9b. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)CONCAT11(param_2,param_1))();
  return;
}



/* ================================================================
 * usb_iep0_done_handler @ CODE:0fc4
 * ============================================================== */

void usb_iep0_done_handler(void)

{
  if (_1_3 != '\0') {
    ep0_in_start_transfer();
    return;
  }
  if (_1_4 != '\0') {
    _1_4 = 0;
    ep0_store_cnf_and_arm_both(OEPCNF0 | 0x20);
    return;
  }
  if (pending_deferred_request == '\x05') {
    USBFADR = pending_USB_device_address;
    pending_deferred_request = '\0';
  }
  ep0_clear_stall_toggle_and_arm();
  return;
}



/* ================================================================
 * ep0_arm_zlp_in_and_out @ CODE:0fea
 * ============================================================== */

void ep0_arm_zlp_in_and_out(void)

{
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * codec_port_cfg3_commit @ CODE:0ff4
 * ============================================================== */

void codec_port_cfg3_commit(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  CPTRXCNF3 = param_1;
  GLOBCTL = GLOBCTL | 1;
  return;
}



/* ================================================================
 * dma0_disable @ CODE:1001
 * ============================================================== */

void dma0_disable(void)

{
  DMACTL0 = DMACTL0 & 0x7f;
  return;
}



/* ================================================================
 * ep0_stall_both @ CODE:1009
 * ============================================================== */

void ep0_stall_both(void)

{
  IEPCNF0 = IEPCNF0 | 8;
  ep0_store_cnf_and_arm_both(OEPCNF0 | 8);
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * timer0_isr_tick @ CODE:101e
 * ============================================================== */

void timer0_isr_tick(void)

{
  EA = 0;
  _4_0 = 1;
  TH0 = 0xce;
  EA = 1;
  return;
}



/* ================================================================
 * toggle_flag_bit1e @ CODE:1028
 * ============================================================== */

void toggle_flag_bit1e(void)

{
  if (_3_6 != '\0') {
    _3_6 = 0;
    return;
  }
  _3_6 = 1;
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



/* ================================================================
 * FUN_CODE_1036 @ CODE:1036
 * ============================================================== */

void FUN_CODE_1036(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1037 @ CODE:1037
 * ============================================================== */

void FUN_CODE_1037(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1038 @ CODE:1038
 * ============================================================== */

void FUN_CODE_1038(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_1039 @ CODE:1039
 * ============================================================== */

void FUN_CODE_1039(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_103a @ CODE:103a
 * ============================================================== */

void FUN_CODE_103a(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_103b @ CODE:103b
 * ============================================================== */

void FUN_CODE_103b(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_103c @ CODE:103c
 * ============================================================== */

void FUN_CODE_103c(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_103d @ CODE:103d
 * ============================================================== */

void FUN_CODE_103d(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_103e @ CODE:103e
 * ============================================================== */

void FUN_CODE_103e(void)

{
  return;
}


