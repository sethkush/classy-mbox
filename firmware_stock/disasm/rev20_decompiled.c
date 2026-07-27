
/* ================================================================
 * FUN_CODE_02ee @ CODE:02ee
 * ============================================================== */

void FUN_CODE_02ee(void)

{
  if (0xd < BANK1_R2 - 1U) {
    BANK1_R2 = 0;
    return;
  }
                    /* WARNING: Could not recover jumptable at 0x02ff. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)((ushort)((BANK1_R2 - 1U) * '\x03') + 0x300))();
  return;
}



/* ================================================================
 * FUN_CODE_0568 @ CODE:0568
 * ============================================================== */

void FUN_CODE_0568(void)

{
  DAT_INTMEM_2c = 4;
  DAT_INTMEM_2d = 0x41;
  FUN_CODE_0c45(0x41,4);
  DAT_INTMEM_2c = 0x12;
  DAT_INTMEM_2d = 0;
  FUN_CODE_0c45(0,0x12);
  return;
}



/* ================================================================
 * FUN_CODE_0582 @ CODE:0582
 * ============================================================== */

void FUN_CODE_0582(void)

{
  FUN_CODE_0c45(DAT_INTMEM_2d,DAT_INTMEM_2c);
  DAT_INTMEM_2c = 0x24;
  DAT_INTMEM_2d = 0x80;
  FUN_CODE_0c45(0x80,0x24);
  return;
}



/* ================================================================
 * FUN_CODE_0728 @ CODE:0728
 * ============================================================== */

void FUN_CODE_0728(char param_1)

{
  short sVar1;
  
  DAT_INTMEM_2f = 0;
  DAT_INTMEM_30 = 0;
  _3_2 = 0;
  _3_3 = 0;
  DAT_INTMEM_2e = param_1;
  FUN_CODE_0e62();
  FUN_CODE_0e18(0xffe2);
  if (DAT_INTMEM_2e == '\x02') {
    ACGFRQ1 = 0x4b;
    ACGFRQ2 = 0x6a;
    ACGFRQ0 = 0x20;
    DMATSH3 = 0x4b;
    DMACTL3 = 0x6a;
    FUN_CODE_0e0f(0x20,0xfff9);
    BANK1_R0 = 2;
    FUN_CODE_0e20();
  }
  else if (DAT_INTMEM_2e == '\x03') {
    FUN_CODE_0dec();
    BANK1_R0 = 3;
    FUN_CODE_0e20();
  }
  else if (DAT_INTMEM_2e == '\x05') {
    GLOBCTL = GLOBCTL & 0xfe;
    CPTRXCNF4 = 1;
    sVar1 = -0x4f;
    FUN_CODE_0deb(GLOBCTL | 1);
    *(undefined1 *)(sVar1 + 1) = 0;
    DMATSL2 = 0x10;
    _3_0 = 1;
    _3_1 = 1;
    FUN_CODE_0e62();
    BANK1_R0 = 5;
    FUN_CODE_0e20();
  }
  else if (DAT_INTMEM_2e == '\x01') {
    ACGCTL = 0xd;
    BANK1_R0 = 1;
    DAT_INTMEM_31 = 4;
    DAT_INTMEM_32 = 0x41;
  }
  FUN_CODE_0c45(DAT_INTMEM_32,DAT_INTMEM_31);
  ACGCTL = ACGCTL | 0xc0;
  IEPDCNTX1 = 0;
  IEPDCNTY1 = 0;
  OEPDCNTX2 = 0;
  OEPDCNTY2 = 0;
  IEPCNF1 = 0xc5;
  OEPCNF2 = 0xc5;
  _3_2 = 1;
  _3_3 = 1;
  FUN_CODE_0e62();
  DAT_INTMEM_2f = '\0';
  DAT_INTMEM_30 = '\0';
  do {
    DAT_INTMEM_30 = DAT_INTMEM_30 + '\x01';
    if (DAT_INTMEM_30 == '\0') {
      DAT_INTMEM_2f = DAT_INTMEM_2f + '\x01';
    }
  } while ((DAT_INTMEM_30 != -1) || (DAT_INTMEM_2f != '\x0f'));
  return;
}



/* ================================================================
 * FUN_CODE_080b @ CODE:080b
 * ============================================================== */

void FUN_CODE_080b(void)

{
  DAT_INTMEM_25 = 0;
  DAT_INTMEM_23 = 0;
  _5_6 = 1;
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  FUN_CODE_0e62();
  FUN_CODE_0dec();
  FUN_CODE_0e17();
  BANK1_R0 = 3;
  ACGCTL = ACGCTL | 0xc0;
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  _3_2 = 1;
  _3_3 = 1;
  FUN_CODE_0e62();
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  _5_7 = 1;
  _3_4 = 1;
  FUN_CODE_0e62();
  DAT_INTMEM_2e = -1;
  do {
    DAT_INTMEM_2e = DAT_INTMEM_2e + -1;
  } while (DAT_INTMEM_2e != '\0');
  _5_7 = 0;
  FUN_CODE_0e62();
  _5_7 = 1;
  FUN_CODE_0e62();
  FUN_CODE_08a6();
  DAT_INTMEM_2e = 0x13;
  DAT_INTMEM_2f = 0x10;
  FUN_CODE_08bd();
  FUN_CODE_08a6();
  DAT_INTMEM_2e = 4;
  DAT_INTMEM_2f = 0x40;
  FUN_CODE_08bd();
  DAT_INTMEM_2e = 1;
  DAT_INTMEM_2f = 1;
  FUN_CODE_08c4();
  DAT_INTMEM_2e = 2;
  DAT_INTMEM_2f = 0x20;
  FUN_CODE_08c4();
  DAT_INTMEM_2e = 3;
  DAT_INTMEM_2f = 0xc;
  FUN_CODE_0c45(0xc,3);
  DAT_INTMEM_2e = 5;
  FUN_CODE_08b3();
  DAT_INTMEM_2e = 6;
  FUN_CODE_08b3();
  DAT_INTMEM_2e = 0x11;
  DAT_INTMEM_2f = 0xff;
  FUN_CODE_0c45(0xff,0x11);
  return;
}



/* ================================================================
 * FUN_CODE_08a6 @ CODE:08a6
 * ============================================================== */

void FUN_CODE_08a6(void)

{
  DAT_INTMEM_2e = 4;
  DAT_INTMEM_2f = 0;
  FUN_CODE_0c45(0,4);
  return;
}



/* ================================================================
 * FUN_CODE_08b3 @ CODE:08b3
 * ============================================================== */

void FUN_CODE_08b3(void)

{
  DAT_INTMEM_2f = 5;
  FUN_CODE_0c45(5,DAT_INTMEM_2e);
  return;
}



/* ================================================================
 * FUN_CODE_08bd @ CODE:08bd
 * ============================================================== */

void FUN_CODE_08bd(void)

{
  FUN_CODE_0c45(DAT_INTMEM_2f,DAT_INTMEM_2e);
  return;
}



/* ================================================================
 * FUN_CODE_08c4 @ CODE:08c4
 * ============================================================== */

void FUN_CODE_08c4(void)

{
  FUN_CODE_0c45(DAT_INTMEM_2f,DAT_INTMEM_2e);
  return;
}



/* ================================================================
 * FUN_CODE_08cb @ CODE:08cb
 * ============================================================== */

void FUN_CODE_08cb(void)

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
  FUN_CODE_0deb(3,0xffd4);
  FUN_CODE_0e17();
  GLOBCTL = GLOBCTL | 1;
  BANK1_R0 = 3;
  DAT_INTMEM_22 = 0;
  _3_6 = 1;
  FUN_CODE_0f0c();
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
  FUN_CODE_0f0c();
  DAT_INTMEM_25 = 0;
  DAT_INTMEM_23 = 0;
  FUN_CODE_0e62();
  return;
}



/* ================================================================
 * FUN_CODE_0970 @ CODE:0970
 * ============================================================== */

void FUN_CODE_0970(void)

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
  BANK1_R1 = 0;
  BANK1_R3 = 0;
  BANK1_R4 = 0xfe;
  BANK1_R2 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0a09 @ CODE:0a09
 * ============================================================== */

void FUN_CODE_0a09(byte param_1)

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
  DAT_INTMEM_27 = '\0';
  DAT_INTMEM_28 = 0xff;
  DAT_INTMEM_29 = -1;
  DAT_INTMEM_2a = 0;
  DAT_INTMEM_2b = 0x10;
  EA = 0;
  USBIMSK = 0;
  _4_2 = 0;
  FUN_CODE_08cb();
  FUN_CODE_0970();
  while (cVar1 = DAT_INTMEM_29, (byte)-(((DAT_INTMEM_29 == '\0') << 7) >> 7) <= DAT_INTMEM_28) {
    DAT_INTMEM_29 = DAT_INTMEM_29 + -1;
    if (cVar1 == '\0') {
      DAT_INTMEM_28 = DAT_INTMEM_28 - 1;
    }
  }
  TR0 = 1;
  EA = 1;
  USBCTL = USBCTL | 0x80;
  do {
    while (_4_0 != '\x01') {
      if (BANK1_R2 != '\0') {
        FUN_CODE_02ee();
      }
    }
    FUN_CODE_0ed5();
    if ((param_1 & 1) != 0) {
      FUN_CODE_0f0c();
      FUN_CODE_0e62();
    }
    if ((_0_1 != '\x01') && (DAT_INTMEM_27 == '\0')) {
      DAT_INTMEM_27 = '\x01';
      BANK1_R2 = '\v';
      FUN_CODE_02ee();
    }
    if ((_0_1 != '\0') && (DAT_INTMEM_27 == '\x01')) {
      DAT_INTMEM_27 = '\0';
      BANK1_R2 = '\f';
      FUN_CODE_02ee();
    }
    _4_0 = '\0';
  } while( true );
}



/* ================================================================
 * FUN_CODE_0b11 @ CODE:0b11
 * ============================================================== */

void FUN_CODE_0b11(void)

{
  BANK3_R3 = 0xfa;
  BANK3_R4 = 0x10;
  return;
}



/* ================================================================
 * FUN_CODE_0b17 @ CODE:0b17
 * ============================================================== */

void FUN_CODE_0b17(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0b1e @ CODE:0b1e
 * ============================================================== */

void FUN_CODE_0b1e(void)

{
  IEPCNF0 = IEPCNF0 & 0xd7;
  OEPCNF0 = OEPCNF0 & 0xd7;
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b2b @ CODE:0b2b
 * ============================================================== */

void FUN_CODE_0b2b(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b36 @ CODE:0b36
 * ============================================================== */

void FUN_CODE_0b36(undefined1 param_1)

{
  *(undefined1 *)CONCAT11(BANK3_R3,param_1) = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b3e @ CODE:0b3e
 * ============================================================== */

void FUN_CODE_0b3e(void)

{
  BANK3_R3 = 0xfa;
  BANK3_R4 = 0x18;
  return;
}



/* ================================================================
 * FUN_CODE_0b45 @ CODE:0b45
 * ============================================================== */

void FUN_CODE_0b45(void)

{
  IEPDCNTX0 = 1;
  _1_3 = 0;
  _1_4 = 1;
  return;
}



/* ================================================================
 * FUN_CODE_0b50 @ CODE:0b50
 * ============================================================== */

void FUN_CODE_0b50(void)

{
  IEPCNF0 = IEPCNF0 & 0xf7;
  OEPCNF0 = OEPCNF0 & 0xf7;
  return;
}



/* ================================================================
 * FUN_CODE_0b5f @ CODE:0b5f
 * ============================================================== */

void FUN_CODE_0b5f(void)

{
  IEPDCNTX0 = 0x80;
  OEPDCNTX0 = 0x80;
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b6e @ CODE:0b6e
 * ============================================================== */

undefined1 FUN_CODE_0b6e(void)

{
  return *(undefined1 *)CONCAT11(BANK3_R1,BANK3_R2);
}



/* ================================================================
 * FUN_CODE_0b77 @ CODE:0b77
 * ============================================================== */

void FUN_CODE_0b77(void)

{
  FUN_CODE_0b8c();
  IEPDCNTX0 = IEPDCNTX0 & 0x7f;
  return;
}



/* ================================================================
 * FUN_CODE_0b82 @ CODE:0b82
 * ============================================================== */

void FUN_CODE_0b82(void)

{
  OEPDCNTX0 = 0;
  IEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b8c @ CODE:0b8c
 * ============================================================== */

void FUN_CODE_0b8c(undefined1 *param_1)

{
  undefined1 uVar1;
  
  BANK3_R0 = 0;
  FUN_CODE_0b3e();
  do {
    if ((BANK1_R1 == '\0') << 7 < '\0') break;
    FUN_CODE_0b6e(BANK1_R1 + -1);
    uVar1 = FUN_CODE_0b17();
    *param_1 = uVar1;
    BANK3_R4 = BANK3_R4 + '\x01';
    if (BANK3_R4 == '\0') {
      BANK3_R3 = BANK3_R3 + '\x01';
    }
    BANK3_R2 = BANK3_R2 + '\x01';
    if (BANK3_R2 == '\0') {
      BANK3_R1 = BANK3_R1 + '\x01';
    }
    BANK1_R1 = BANK1_R1 + -1;
    if ((BANK1_R1 == '\0') && (BANK1_R3 != '\0')) {
      BANK1_R1 = -1;
      BANK1_R3 = BANK1_R3 + -1;
    }
    BANK3_R0 = BANK3_R0 + 1;
  } while (BANK3_R0 != 8);
  IEPDCNTX0 = BANK3_R0 | 0x80;
  _1_3 = 1;
  _1_4 = 0;
  if ((BANK1_R1 == '\0') && (BANK1_R3 == '\0')) {
    if ((BANK3_R0 == 8) && (_1_5 != '\0')) {
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
 * FUN_CODE_0c45 @ CODE:0c45
 * ============================================================== */

void FUN_CODE_0c45(undefined1 param_1)

{
  char cVar1;
  char cVar2;
  char cVar3;
  byte bVar4;
  
  cVar3 = '\b';
  cVar2 = '\x01';
  _5_7 = 0;
  DAT_INTMEM_33 = param_1;
  FUN_CODE_0e62(BANK0_R5,0x20);
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
  FUN_CODE_0e62();
  return;
}



/* ================================================================
 * FUN_CODE_0cdd @ CODE:0cdd
 * ============================================================== */

undefined1 FUN_CODE_0cdd(void)

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
 * FUN_CODE_0d6b @ CODE:0d6b
 * ============================================================== */

byte FUN_CODE_0d6b(void)

{
  byte bVar1;
  
  if ((DAT_EXTMEM_ff2f + 1 <= BANK1_R3) ||
     ((DAT_EXTMEM_ff2f == BANK1_R3 && (DAT_EXTMEM_ff2e + 1 <= BANK1_R1)))) {
    BANK1_R1 = DAT_EXTMEM_ff2e;
    BANK1_R3 = DAT_EXTMEM_ff2f;
    _1_5 = 0;
  }
  bVar1 = BANK1_R1 - DAT_EXTMEM_ff2e;
  if (((BANK1_R1 < DAT_EXTMEM_ff2e) << 7 < '\0') ||
     ((bVar1 = DAT_EXTMEM_ff2e, DAT_EXTMEM_ff2e == BANK1_R1 &&
      (bVar1 = BANK1_R3 - DAT_EXTMEM_ff2f, BANK1_R3 < DAT_EXTMEM_ff2f)))) {
    _1_5 = 1;
  }
  return bVar1;
}



/* ================================================================
 * FUN_CODE_0dac @ CODE:0dac
 * ============================================================== */

undefined1 FUN_CODE_0dac(undefined1 param_1)

{
  undefined1 *puVar1;
  
  EA = 0;
  puVar1 = (undefined1 *)
           CONCAT11('\f' - (((0x6cU < (byte)(VECINT * '\x02')) << 7) >> 7),VECINT * '\x02' + 0x93);
  FUN_CODE_0f96(puVar1[1],BANK2_R6,0xff,*puVar1,VECINT);
  VECINT = 0;
  EA = 1;
  return param_1;
}



/* ================================================================
 * FUN_CODE_0deb @ CODE:0deb
 * ============================================================== */

void FUN_CODE_0deb(undefined1 param_1,undefined1 *param_2)

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
 * FUN_CODE_0dec @ CODE:0dec
 * ============================================================== */

void FUN_CODE_0dec(void)

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
 * FUN_CODE_0e0f @ CODE:0e0f
 * ============================================================== */

void FUN_CODE_0e0f(undefined1 param_1,undefined1 *param_2)

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
 * FUN_CODE_0e20 @ CODE:0e20
 * ============================================================== */

void FUN_CODE_0e20(void)

{
  DAT_INTMEM_31 = 4;
  DAT_INTMEM_32 = 0x40;
  return;
}



/* ================================================================
 * FUN_CODE_0e27 @ CODE:0e27
 * ============================================================== */

void FUN_CODE_0e27(void)

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
 * FUN_CODE_0e62 @ CODE:0e62
 * ============================================================== */

void FUN_CODE_0e62(void)

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
 * FUN_CODE_0e9d @ CODE:0e9d
 * ============================================================== */

void FUN_CODE_0e9d(void)

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
 * FUN_CODE_0ed5 @ CODE:0ed5
 * ============================================================== */

void FUN_CODE_0ed5(void)

{
  byte bVar1;
  
  bVar1 = P3;
  if (bVar1 == DAT_INTMEM_20) {
    return;
  }
  if ((_0_5 != '\x01') && ((bVar1 >> 5 & 1) != 0)) {
    FUN_CODE_1028(0);
    BANK0_R6 = BANK0_R6 | 1;
  }
  if ((_0_3 != '\x01') && ((bVar1 >> 3 & 1) != 0)) {
    FUN_CODE_0e27();
    BANK0_R6 = BANK0_R6 | 1;
  }
  if ((_0_4 != '\x01') && ((bVar1 >> 4 & 1) != 0)) {
    FUN_CODE_0e9d();
    BANK0_R6 = BANK0_R6 | 1;
  }
  DAT_INTMEM_20 = bVar1;
  return;
}



/* ================================================================
 * FUN_CODE_0f0c @ CODE:0f0c
 * ============================================================== */

void FUN_CODE_0f0c(void)

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
 * FUN_CODE_0f70 @ CODE:0f70
 * ============================================================== */

void FUN_CODE_0f70(char param_1)

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
 * FUN_CODE_0f96 @ CODE:0f96
 * ============================================================== */

void FUN_CODE_0f96(undefined1 param_1,undefined1 param_2)

{
                    /* WARNING: Could not recover jumptable at 0x0f9b. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)CONCAT11(param_2,param_1))();
  return;
}



/* ================================================================
 * FUN_CODE_0fea @ CODE:0fea
 * ============================================================== */

void FUN_CODE_0fea(void)

{
  IEPDCNTX0 = 0;
  OEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0ff4 @ CODE:0ff4
 * ============================================================== */

void FUN_CODE_0ff4(undefined1 param_1,undefined1 *param_2)

{
  *param_2 = param_1;
  CPTRXCNF3 = param_1;
  GLOBCTL = GLOBCTL | 1;
  return;
}



/* ================================================================
 * FUN_CODE_1001 @ CODE:1001
 * ============================================================== */

void FUN_CODE_1001(void)

{
  DMACTL0 = DMACTL0 & 0x7f;
  return;
}



/* ================================================================
 * FUN_CODE_1009 @ CODE:1009
 * ============================================================== */

void FUN_CODE_1009(void)

{
  IEPCNF0 = IEPCNF0 | 8;
  FUN_CODE_0b2b(OEPCNF0 | 8);
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_101e @ CODE:101e
 * ============================================================== */

void FUN_CODE_101e(void)

{
  EA = 0;
  _4_0 = 1;
  TH0 = 0xce;
  EA = 1;
  return;
}



/* ================================================================
 * FUN_CODE_1028 @ CODE:1028
 * ============================================================== */

void FUN_CODE_1028(void)

{
  if (_3_6 != '\0') {
    _3_6 = 0;
    return;
  }
  _3_6 = 1;
  return;
}


