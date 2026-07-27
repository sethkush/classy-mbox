
/* ================================================================
 * FUN_CODE_02f3 @ CODE:02f3
 * ============================================================== */

void FUN_CODE_02f3(void)

{
  ushort uVar1;
  
  if (0xd < BANK1_R2 - 1U) {
    BANK1_R2 = 0;
    return;
  }
  uVar1 = (ushort)(BANK1_R2 - 1U) * 3;
                    /* WARNING: Could not recover jumptable at 0x030b. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)(CONCAT11((char)(uVar1 >> 8) + '\x03',0xc) + (uVar1 & 0xff)))();
  return;
}



/* ================================================================
 * FUN_CODE_0567 @ CODE:0567
 * ============================================================== */

void FUN_CODE_0567(void)

{
  DAT_INTMEM_2c = 4;
  DAT_INTMEM_2d = 0x41;
  FUN_CODE_0c31(0x41,4);
  return;
}



/* ================================================================
 * FUN_CODE_0575 @ CODE:0575
 * ============================================================== */

void FUN_CODE_0575(void)

{
  FUN_CODE_0c31(DAT_INTMEM_2d,DAT_INTMEM_2c);
  return;
}



/* ================================================================
 * FUN_CODE_070f @ CODE:070f
 * ============================================================== */

void FUN_CODE_070f(char param_1)

{
  short sVar1;
  
  DAT_INTMEM_2f = 0;
  DAT_INTMEM_30 = 0;
  _3_2 = 0;
  _3_3 = 0;
  DAT_INTMEM_2e = param_1;
  FUN_CODE_0e56();
  FUN_CODE_0ef4(0xffe2);
  if (DAT_INTMEM_2e == '\x02') {
    ACGFRQ1 = 0x4b;
    ACGFRQ2 = 0x6a;
    ACGFRQ0 = 0x20;
    DMATSH3 = 0x4b;
    DMACTL3 = 0x6a;
    FUN_CODE_0ee8(0x20);
    BANK1_R0 = 2;
  }
  else if (DAT_INTMEM_2e == '\x03') {
    FUN_CODE_0ec8();
    BANK1_R0 = 3;
  }
  else {
    if (DAT_INTMEM_2e != '\x05') {
      if (DAT_INTMEM_2e == '\x01') {
        ACGCTL = 0xd;
        BANK1_R0 = 1;
        DAT_INTMEM_31 = 4;
        DAT_INTMEM_32 = 0x41;
      }
      goto LAB_CODE_07a6;
    }
    GLOBCTL = GLOBCTL & 0xfe;
    CPTRXCNF4 = 1;
    sVar1 = -0x4f;
    FUN_CODE_0ec7(GLOBCTL | 1);
    *(undefined1 *)(sVar1 + 1) = 0;
    DMATSL2 = 0x10;
    _3_0 = 1;
    _3_1 = 1;
    FUN_CODE_0e56();
    BANK1_R0 = 5;
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
  FUN_CODE_0e56();
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
 * FUN_CODE_07ec @ CODE:07ec
 * ============================================================== */

void FUN_CODE_07ec(void)

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
  FUN_CODE_0ec7(3,0xffd4);
  FUN_CODE_0ef3();
  GLOBCTL = GLOBCTL | 1;
  BANK1_R0 = 3;
  DAT_INTMEM_22 = 0;
  _3_6 = 1;
  FUN_CODE_0efc();
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
  FUN_CODE_0efc();
  DAT_INTMEM_25 = 0;
  DAT_INTMEM_23 = 0;
  FUN_CODE_0e56();
  return;
}



/* ================================================================
 * FUN_CODE_0891 @ CODE:0891
 * ============================================================== */

void FUN_CODE_0891(void)

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
 * FUN_CODE_092a @ CODE:092a
 * ============================================================== */

void FUN_CODE_092a(void)

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
  FUN_CODE_0a3f();
  return;
}



/* ================================================================
 * FUN_CODE_09b6 @ CODE:09b6
 * ============================================================== */

void FUN_CODE_09b6(void)

{
  char cVar1;
  
  DAT_INTMEM_25 = 0;
  DAT_INTMEM_23 = 0;
  _5_6 = 1;
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  FUN_CODE_0e56();
  FUN_CODE_0ec8();
  FUN_CODE_0ef3();
  BANK1_R0 = 3;
  ACGCTL = ACGCTL | 0xc0;
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  _3_2 = 1;
  _3_3 = 1;
  FUN_CODE_0e56();
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  _5_7 = 1;
  _3_4 = 1;
  FUN_CODE_0e56();
  cVar1 = -1;
  do {
    cVar1 = cVar1 + -1;
  } while (cVar1 != '\0');
  _5_7 = 0;
  FUN_CODE_0e56();
  _5_7 = 1;
  FUN_CODE_0e56();
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
 * FUN_CODE_0a3f @ CODE:0a3f
 * ============================================================== */

void FUN_CODE_0a3f(byte param_1)

{
  char cVar1;
  
  DAT_INTMEM_27 = '\0';
  DAT_INTMEM_28 = 0xff;
  DAT_INTMEM_29 = -1;
  DAT_INTMEM_2a = 0;
  DAT_INTMEM_2b = 0x10;
  EA = 0;
  USBIMSK = 0;
  _4_2 = 0;
  FUN_CODE_07ec();
  FUN_CODE_0891();
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
        FUN_CODE_02f3();
      }
    }
    FUN_CODE_0f31();
    if ((param_1 & 1) != 0) {
      FUN_CODE_0efc();
      FUN_CODE_0e56();
    }
    if ((_0_1 != '\x01') && (DAT_INTMEM_27 == '\0')) {
      DAT_INTMEM_27 = '\x01';
      BANK1_R2 = '\v';
      FUN_CODE_02f3();
    }
    if ((_0_1 != '\0') && (DAT_INTMEM_27 == '\x01')) {
      DAT_INTMEM_27 = '\0';
      BANK1_R2 = '\f';
      FUN_CODE_02f3();
    }
    _4_0 = '\0';
  } while( true );
}



/* ================================================================
 * FUN_CODE_0abb @ CODE:0abb
 * ============================================================== */

void FUN_CODE_0abb(undefined1 *param_1)

{
  undefined1 uVar1;
  
  BANK3_R0 = 0;
  FUN_CODE_0b37();
  do {
    if ((BANK1_R1 == '\0') << 7 < '\0') break;
    FUN_CODE_0b6e(BANK1_R1 + -1);
    uVar1 = FUN_CODE_0b25(*param_1);
    *param_1 = uVar1;
    BANK3_R6 = BANK3_R6 + '\x01';
    if (BANK3_R6 == '\0') {
      BANK3_R5 = BANK3_R5 + '\x01';
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
 * FUN_CODE_0b1f @ CODE:0b1f
 * ============================================================== */

void FUN_CODE_0b1f(void)

{
  BANK3_R5 = 0xfa;
  BANK3_R6 = 0x10;
  return;
}



/* ================================================================
 * FUN_CODE_0b25 @ CODE:0b25
 * ============================================================== */

void FUN_CODE_0b25(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0b2c @ CODE:0b2c
 * ============================================================== */

void FUN_CODE_0b2c(undefined1 param_1,undefined1 *param_2)

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
 * FUN_CODE_0b3e @ CODE:0b3e
 * ============================================================== */

void FUN_CODE_0b3e(void)

{
  IEPCNF0 = IEPCNF0 & 0xf7;
  OEPCNF0 = OEPCNF0 & 0xf7;
  return;
}



/* ================================================================
 * FUN_CODE_0b4d @ CODE:0b4d
 * ============================================================== */

byte FUN_CODE_0b4d(void)

{
  IEPCNF0 = IEPCNF0 & 0xd7;
  return OEPCNF0 & 0xd7;
}



/* ================================================================
 * FUN_CODE_0b5b @ CODE:0b5b
 * ============================================================== */

void FUN_CODE_0b5b(undefined1 param_1)

{
  *(undefined1 *)CONCAT11(BANK3_R5,param_1) = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b63 @ CODE:0b63
 * ============================================================== */

void FUN_CODE_0b63(void)

{
  FUN_CODE_0abb();
  IEPDCNTX0 = IEPDCNTX0 & 0x7f;
  return;
}



/* ================================================================
 * FUN_CODE_0b6e @ CODE:0b6e
 * ============================================================== */

void FUN_CODE_0b6e(void)

{
  return;
}



/* ================================================================
 * FUN_CODE_0b75 @ CODE:0b75
 * ============================================================== */

void FUN_CODE_0b75(void)

{
  OEPDCNTX0 = 0;
  IEPDCNTX0 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_0b7f @ CODE:0b7f
 * ============================================================== */

byte FUN_CODE_0b7f(char param_1,byte param_2,byte param_3,byte param_4)

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
 * FUN_CODE_0bd4 @ CODE:0bd4
 * ============================================================== */

void FUN_CODE_0bd4(undefined1 param_1,undefined1 param_2)

{
                    /* WARNING: Could not recover jumptable at 0x0bd9. Too many branches */
                    /* WARNING: Treating indirect jump as call */
  (*(code *)CONCAT11(param_2,param_1))();
  return;
}



/* ================================================================
 * FUN_CODE_0bda @ CODE:0bda
 * ============================================================== */

void FUN_CODE_0bda(undefined1 param_1)

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
  FUN_CODE_0e56(BANK0_R7,0x20);
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
  FUN_CODE_0e56();
  return;
}



/* ================================================================
 * FUN_CODE_0d11 @ CODE:0d11
 * ============================================================== */

undefined1 FUN_CODE_0d11(void)

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
 * FUN_CODE_0d9e @ CODE:0d9e
 * ============================================================== */

byte FUN_CODE_0d9e(void)

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
 * FUN_CODE_0ddf @ CODE:0ddf
 * ============================================================== */

undefined1 FUN_CODE_0ddf(undefined1 param_1)

{
  undefined1 *puVar1;
  
  EA = 0;
  puVar1 = (undefined1 *)
           CONCAT11('\f' - (((0x82U < (byte)(VECINT * '\x02')) << 7) >> 7),VECINT * '\x02' + 0x7d);
  FUN_CODE_0bd4(puVar1[1],BANK2_R6,*puVar1);
  VECINT = 0;
  EA = 1;
  return param_1;
}



/* ================================================================
 * FUN_CODE_0e1b @ CODE:0e1b
 * ============================================================== */

void FUN_CODE_0e1b(void)

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
 * FUN_CODE_0e56 @ CODE:0e56
 * ============================================================== */

void FUN_CODE_0e56(void)

{
  byte bVar1;
  bool bVar2;
  char cVar3;
  char cVar4;
  byte bVar5;
  
  cVar4 = '\b';
  _6_0 = 1;
  bVar2 = true;
  bVar5 = DAT_INTMEM_23;
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
 * FUN_CODE_0e8f @ CODE:0e8f
 * ============================================================== */

void FUN_CODE_0e8f(void)

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
 * FUN_CODE_0ec7 @ CODE:0ec7
 * ============================================================== */

void FUN_CODE_0ec7(undefined1 param_1,undefined1 *param_2)

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
 * FUN_CODE_0ec8 @ CODE:0ec8
 * ============================================================== */

void FUN_CODE_0ec8(void)

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
 * FUN_CODE_0ee8 @ CODE:0ee8
 * ============================================================== */

void FUN_CODE_0ee8(undefined1 param_1)

{
  DMATSL3 = param_1;
  ACGCTL = 6;
  return;
}



/* ================================================================
 * FUN_CODE_0ef3 @ CODE:0ef3
 * ============================================================== */

void FUN_CODE_0ef3(short param_1)

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
 * FUN_CODE_0efc @ CODE:0efc
 * ============================================================== */

void FUN_CODE_0efc(void)

{
  byte bVar1;
  char cVar2;
  char cVar3;
  byte bVar4;
  
  cVar3 = '\b';
  bVar4 = P1;
  P1 = bVar4 & 0xbf;
  bVar4 = DAT_INTMEM_22;
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
 * FUN_CODE_0f31 @ CODE:0f31
 * ============================================================== */

void FUN_CODE_0f31(void)

{
  byte bVar1;
  
  bVar1 = P3;
  if (bVar1 == DAT_INTMEM_20) {
    return;
  }
  if ((_0_5 != '\x01') && ((bVar1 >> 5 & 1) != 0)) {
    FUN_CODE_1020(0);
    BANK0_R7 = BANK0_R7 | 1;
  }
  if ((_0_3 != '\x01') && ((bVar1 >> 3 & 1) != 0)) {
    FUN_CODE_0e1b();
    BANK0_R7 = BANK0_R7 | 1;
  }
  if ((_0_4 != '\x01') && ((bVar1 >> 4 & 1) != 0)) {
    FUN_CODE_0e8f();
    BANK0_R7 = BANK0_R7 | 1;
  }
  DAT_INTMEM_20 = bVar1;
  return;
}



/* ================================================================
 * FUN_CODE_0fe2 @ CODE:0fe2
 * ============================================================== */

void FUN_CODE_0fe2(undefined1 param_1)

{
  CPTCNF3 = param_1;
  CPTRXCNF3 = param_1;
  GLOBCTL = GLOBCTL | 1;
  return;
}



/* ================================================================
 * FUN_CODE_0ff2 @ CODE:0ff2
 * ============================================================== */

void FUN_CODE_0ff2(void)

{
  DMACTL0 = DMACTL0 & 0x7f;
  return;
}



/* ================================================================
 * FUN_CODE_0ffa @ CODE:0ffa
 * ============================================================== */

void FUN_CODE_0ffa(void)

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
  FUN_CODE_0b2c(OEPCNF0 | 8);
  _1_3 = 0;
  _1_4 = 0;
  return;
}



/* ================================================================
 * FUN_CODE_1016 @ CODE:1016
 * ============================================================== */

void FUN_CODE_1016(void)

{
  EA = 0;
  _4_0 = 1;
  TH0 = 0xce;
  EA = 1;
  return;
}



/* ================================================================
 * FUN_CODE_1020 @ CODE:1020
 * ============================================================== */

void FUN_CODE_1020(void)

{
  if (_3_6 != '\0') {
    _3_6 = 0;
    return;
  }
  _3_6 = 1;
  return;
}


