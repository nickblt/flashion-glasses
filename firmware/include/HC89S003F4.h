/*
 * HC89S003F4.h — register definitions for the Shanghai Holychip HC89S003A/001A
 *
 * Hand-written for SDCC, with Keil C51 compatibility, because the vendor's own
 * header ships only inside Keil's device database.
 *
 * PROVENANCE — every address below is transcribed from the English
 * HC89S003A/001A datasheet, Ver1.02 (HC89S003A_001A__Datasheet_Ver1.02_en.pdf),
 *   §3.3 "Special function registers (SFR)" (direct SFR table + the five
 *   extension XSFR banks) and §8.2 "Interrupt summary" (vector numbers).
 * Nothing here is inferred or guessed. Registers the datasheet marks "-" are
 * omitted rather than filled in.
 *
 * The symbol set was scoped against github.com/yiyidijiyi/smartpad_hc89s003,
 * a working HC89S003F4 peripheral library whose sources #include a header of
 * this name. Eight of its nine modules compile unmodified against this one;
 * the flash module needs the IAP_CMD porting change described below.
 *
 * Two OCR artifacts in the English datasheet, corrected here against the
 * adjacent prose and §8.2, which both disagree with the garbled table label:
 *   IE  bit 3 printed "BTV" -> ET1   ("T1 interrupt enable bit")
 *   IE1 bit 2 printed "ETS" -> ET4   ("T4 interrupt enable bit")
 *
 * ONE OPEN QUESTION — see IAP_CMD_WRITE() below.
 */

#ifndef HC89S003F4_H
#define HC89S003F4_H

/* ------------------------------------------------------------------ *
 * Compiler abstraction
 * ------------------------------------------------------------------ */

#ifdef __SDCC
  #define SFR(name, addr)   __sfr  __at (addr) name
  #define SBIT(name, addr)  __sbit __at (addr) name
  #define XSFR(addr)        (*(__xdata volatile unsigned char *)(addr))
#else
  /* Keil C51 */
  #define SFR(name, addr)   sfr  name = addr
  #define SBIT(name, addr)  sbit name = addr
  #define XSFR(addr)        (*(volatile unsigned char xdata *)(addr))
#endif

/*
 * XSFRs are reached with MOVX (datasheet §3.3.1.2), so they are expressed as
 * dereferenced xdata pointers rather than as located variables. That makes this
 * header safe to include from every translation unit, so the vendor's
 * "#define ALLOCATE_EXTERN" dance is unnecessary — defining it is harmless.
 */

/* ------------------------------------------------------------------ *
 * Direct-addressing SFRs (datasheet §3.3.1.1)
 * ------------------------------------------------------------------ */

/* 0x80 */
SFR(P0,         0x80);
SFR(SP,         0x81);
SFR(DPL,        0x82);
SFR(DPH,        0x83);
SFR(PCON,       0x87);
/* 0x88 */
SFR(TCON,       0x88);
SFR(TMOD,       0x89);
SFR(TL0,        0x8A);
SFR(TL1,        0x8B);
SFR(TH0,        0x8C);
SFR(TH1,        0x8D);
SFR(CLKSWR,     0x8E);
SFR(CLKCON,     0x8F);
/* 0x90 */
SFR(P1,         0x90);
SFR(PINTF0,     0x96);
SFR(PINTF1,     0x97);
/* 0x98 */
SFR(SCON,       0x98);
SFR(SBUF,       0x99);
SFR(SADDR,      0x9A);
SFR(SADEN,      0x9B);
SFR(SCON2,      0x9E);
/* 0xA0 */
SFR(P2,         0xA0);
SFR(INSCON,     0xA2);
SFR(IICCON,     0xA6);
SFR(IICSTA,     0xA7);
/* 0xA8 */
SFR(IE,         0xA8);
SFR(IP0,        0xA9);
SFR(IP1,        0xAA);
SFR(SPDAT,      0xAB);
SFR(SPCTL,      0xAC);
SFR(SPSTAT,     0xAD);
SFR(IICDAT,     0xAE);
SFR(IICADR,     0xAF);
/* 0xB0 */
SFR(IP4,        0xB1);
SFR(ADCC0,      0xB4);
SFR(ADCC1,      0xB5);
SFR(ADCRL,      0xB6);
SFR(ADCRH,      0xB7);
/* 0xB8 */
SFR(IE1,        0xB8);
SFR(IP2,        0xB9);
SFR(IP3,        0xBA);
SFR(LVDC,       0xBB);
SFR(LVDCMP,     0xBC);
SFR(WDTC,       0xBD);
SFR(CRCL,       0xBE);
SFR(CRCH,       0xBF);
/* 0xC0 */
SFR(T5CON,      0xC1);
SFR(TL5,        0xC2);
SFR(TH5,        0xC3);
SFR(RCAP5L,     0xC4);
SFR(RCAP5H,     0xC5);
/* 0xC8 */
SFR(T3CON,      0xC9);
SFR(TL3,        0xCA);
SFR(TH3,        0xCB);
SFR(T4CON,      0xCC);
SFR(TL4,        0xCD);
SFR(TH4,        0xCE);
/* 0xD0 */
SFR(PSW,        0xD0);
SFR(LCDCON,     0xD1);
/* 0xD8 */
SFR(PWM0C,      0xDA);
SFR(PWM1C,      0xDB);
SFR(PWM2C,      0xDC);
SFR(PWM3C,      0xDD);
SFR(PWM3P,      0xDE);
SFR(PWM3D,      0xDF);
/* 0xE0 */
SFR(ACC,        0xE0);
SFR(PWM0EN,     0xE1);
SFR(PWM0PL,     0xE2);
SFR(PWM0PH,     0xE3);
SFR(PWM0DL,     0xE4);
SFR(PWM0DH,     0xE5);
SFR(PWM0DTL,    0xE6);
SFR(PWM0DTH,    0xE7);
/* 0xE8 */
SFR(PWM1EN,     0xE9);
SFR(PWM1PL,     0xEA);
SFR(PWM1PH,     0xEB);
SFR(PWM1DL,     0xEC);
SFR(PWM1DH,     0xED);
SFR(PWM1DTL,    0xEE);
SFR(PWM1DTH,    0xEF);
/* 0xF0 */
SFR(B,          0xF0);
SFR(PWM2EN,     0xF1);
SFR(PWM2PL,     0xF2);
SFR(PWM2PH,     0xF3);
SFR(PWM2DL,     0xF4);
SFR(PWM2DH,     0xF5);
SFR(PWM2DTL,    0xF6);
SFR(PWM2DTH,    0xF7);
/* 0xF8 — the IAP / flash block */
SFR(RSTFR,      0xF8);
SFR(IAP_ADDRL,  0xF9);
SFR(IAP_ADDRH,  0xFA);
SFR(IAP_DATA,   0xFB);
SFR(IAP_CMDL,   0xFC);
SFR(IAP_CMDH,   0xFD);

/*
 * ADCR — the 12-bit ADC result as one value.
 *
 * ⚠ ALIGNMENT-DEPENDENT. Per §12, the packing is chosen by the ALIGN bit:
 *     ALIGN = 1 (right-aligned): ADCRH[3:0] = high bits, ADCRL[7:0] = low 8
 *     ALIGN = 0 (left-aligned):  ADCRH[7:0] = high 8,    ADCRL[3:0] = low bits
 * This macro implements the ALIGN = 1 case only, which is what makes
 * `result = ADCR` mean what it looks like. Under ALIGN = 0 read ADCRH and ADCRL
 * yourself. The datasheet documents no required read order between the two.
 */
#define ADCR  (((unsigned int)(ADCRH & 0x0F) << 8) | (unsigned int)ADCRL)

/* ------------------------------------------------------------------ *
 * Bit-addressable SFRs
 *
 * Only SFRs at addresses divisible by 8 are bit-addressable on an 8051, which
 * here means P0, TCON, P1, SCON, P2, IE, IE1, PSW, ACC, B and RSTFR.
 * ------------------------------------------------------------------ */

/* P0 (0x80) */
SBIT(P0_0, 0x80); SBIT(P0_1, 0x81); SBIT(P0_2, 0x82); SBIT(P0_3, 0x83);
SBIT(P0_4, 0x84); SBIT(P0_5, 0x85); SBIT(P0_6, 0x86); SBIT(P0_7, 0x87);

/*
 * P1 (0x90) — only two pins exist. The datasheet never mentions a P1.2 or
 * above, and P1M0 is the only P1 mode register, so bits 2-7 are deliberately
 * not declared here.
 */
SBIT(P1_0, 0x90); SBIT(P1_1, 0x91);

/* P2 (0xA0) */
SBIT(P2_0, 0xA0); SBIT(P2_1, 0xA1); SBIT(P2_2, 0xA2); SBIT(P2_3, 0xA3);
SBIT(P2_4, 0xA4); SBIT(P2_5, 0xA5); SBIT(P2_6, 0xA6); SBIT(P2_7, 0xA7);

/*
 * TCON (0x88). NOT the classic 8051 layout: the datasheet marks bits 3-0
 * reserved, so there are no IE0/IT0/IE1/IT1 bits here. External-interrupt edge
 * flags live in INT0F/INT1F instead, which also avoids colliding with the IE1
 * register at 0xB8.
 */
SBIT(TR0, 0x8C); SBIT(TF0, 0x8D); SBIT(TR1, 0x8E); SBIT(TF1, 0x8F);

/* IE (0xA8) */
SBIT(EX0,  0xA8); SBIT(ET0,  0xA9); SBIT(EX1,  0xAA); SBIT(ET1,  0xAB);
SBIT(ES1,  0xAC); SBIT(EWDT, 0xAD); SBIT(ES2,  0xAE); SBIT(EA,   0xAF);

/* IE1 (0xB8) */
SBIT(ESPI,   0xB8); SBIT(ET3,  0xB9); SBIT(ET4,   0xBA); SBIT(EIIC,   0xBB);
SBIT(ET5,    0xBC); SBIT(EADC, 0xBD); SBIT(EX2_7, 0xBE); SBIT(EX8_15, 0xBF);

/* PSW (0xD0) */
SBIT(P,   0xD0); SBIT(F1,  0xD1); SBIT(OV, 0xD2); SBIT(RS0, 0xD3);
SBIT(RS1, 0xD4); SBIT(F0,  0xD5); SBIT(AC, 0xD6); SBIT(CY,  0xD7);

/* ------------------------------------------------------------------ *
 * Extension XSFRs, base 0xFE80 (datasheet §3.3.1.2)
 * ------------------------------------------------------------------ */

#define TCON1           XSFR(0xFE80)
#define T5CON1          XSFR(0xFE85)
#define T5CON2          XSFR(0xFE86)
#define S2CON           XSFR(0xFE88)
#define S2CON2          XSFR(0xFE89)
#define S2BUF           XSFR(0xFE8A)
#define BRTSEL          XSFR(0xFE8B)
#define CLKDIV          XSFR(0xFE91)
#define FREQ_CLK        XSFR(0xFE92)
#define CLKOUT          XSFR(0xFE93)
#define XTALCFG         XSFR(0xFE94)
#define SPOV_RSTEN      XSFR(0xFE95)
#define XTALCFG1        XSFR(0xFE96)
#define PORB_IAP        XSFR(0xFE97)
#define ADCWC           XSFR(0xFE98)
#define ADCC3           XSFR(0xFE9A)
#define ADCC2           XSFR(0xFE9B)
#define ADCDLYH         XSFR(0xFE9C)
#define ADCDLYL         XSFR(0xFE9D)
#define WDTCCR          XSFR(0xFEA0)
#define CRCC            XSFR(0xFEA2)
#define BORC            XSFR(0xFEA4)
#define BORDBC          XSFR(0xFEA5)
#define LVDDBC          XSFR(0xFEA7)
#define RSTDBC          XSFR(0xFEAA)
#define PITS0           XSFR(0xFEB0)
#define PITS1           XSFR(0xFEB1)
#define PITS2           XSFR(0xFEB2)
#define PITS3           XSFR(0xFEB3)
#define PINTE0          XSFR(0xFEB8)
#define PINTE1          XSFR(0xFEB9)
#define INT01_PINS      XSFR(0xFEBC)
#define TRMEN           XSFR(0xFEBD)
#define TRMV            XSFR(0xFEBE)

/* ------------------------------------------------------------------ *
 * Extension XSFRs, base 0xFF00 — port mode select
 *
 * PxM0 holds pins 0 and 1, PxM1 pins 2 and 3, PxM2 pins 4 and 5, PxM3 pins 6
 * and 7. Low nibble is the even pin, high nibble the odd pin. One nibble per
 * pin, values per GPIO_MODE_* below.
 *
 * P1 has only P1M0 — the datasheet lists no P1M1..P1M3.
 * ------------------------------------------------------------------ */

#define P0M0            XSFR(0xFF00)
#define P0M1            XSFR(0xFF01)
#define P0M2            XSFR(0xFF02)
#define P0M3            XSFR(0xFF03)
#define P0LPU           XSFR(0xFF05)
#define P1M0            XSFR(0xFF08)
#define P2M0            XSFR(0xFF10)
#define P2M1            XSFR(0xFF11)
#define P2M2            XSFR(0xFF12)
#define P2M3            XSFR(0xFF13)

/* ------------------------------------------------------------------ *
 * Extension XSFRs, base 0xFF40 — debounce, output latch, comparator
 * ------------------------------------------------------------------ */

#define P00DBC          XSFR(0xFF40)
#define P01DBC          XSFR(0xFF41)
#define P02DBC          XSFR(0xFF42)
#define P0OUT           XSFR(0xFF50)
#define P1OUT           XSFR(0xFF51)
#define P2OUT           XSFR(0xFF52)
#define COMP0EN         XSFR(0xFF60)
#define COMP1EN         XSFR(0xFF61)
#define COMP2EN         XSFR(0xFF62)

/* ------------------------------------------------------------------ *
 * Extension XSFRs, base 0xFF80 — peripheral pin remap
 * ------------------------------------------------------------------ */

#define T0_MAP          XSFR(0xFF80)
#define T1_MAP          XSFR(0xFF81)
#define T3_MAP          XSFR(0xFF83)
#define T4_MAP          XSFR(0xFF84)
#define T5_MAP          XSFR(0xFF85)
#define CLKO_MAP        XSFR(0xFF8F)
#define PWM0_MAP        XSFR(0xFF90)
#define PWM01_MAP       XSFR(0xFF91)
#define PWM1_MAP        XSFR(0xFF94)
#define PWM11_MAP       XSFR(0xFF95)
#define PWM2_MAP        XSFR(0xFF98)
#define PWM21_MAP       XSFR(0xFF99)
#define PWM3_MAP        XSFR(0xFF9C)
#define TXD_MAP         XSFR(0xFFA0)
#define RXD_MAP         XSFR(0xFFA1)
#define SCL_MAP         XSFR(0xFFA2)
#define SDA_MAP         XSFR(0xFFA3)
#define SS_MAP          XSFR(0xFFA4)
#define SCK_MAP         XSFR(0xFFA5)
#define MOSI_MAP        XSFR(0xFFA6)
#define MISO_MAP        XSFR(0xFFA7)
#define TXD2_MAP        XSFR(0xFFA8)
#define RXD2_MAP        XSFR(0xFFA9)

/* ------------------------------------------------------------------ *
 * Extension XSFRs, base 0xFFC0
 * ------------------------------------------------------------------ */

#define PWMENA          XSFR(0xFFF8)
#define PWMCON0         XSFR(0xFFF9)

/* ------------------------------------------------------------------ *
 * GPIO mode nibble values
 *
 * Datasheet §7.4.6 note 8 warns that P2.7, P2.5, P2.4 and P2.3 configure
 * differently from the other pins — check it before trusting these there.
 * ------------------------------------------------------------------ */

#define GPIO_MODE_IN               0x00  /* input, no Schmitt trigger   */
#define GPIO_MODE_IN_PULLDOWN      0x01
#define GPIO_MODE_IN_PULLUP        0x02
#define GPIO_MODE_ANALOG           0x03
#define GPIO_MODE_IN_SCHMITT       0x04
#define GPIO_MODE_IN_PULLDOWN_ST   0x05
#define GPIO_MODE_IN_PULLUP_ST     0x06
#define GPIO_MODE_OUT_PP           0x08  /* push-pull                   */
#define GPIO_MODE_OUT_OD           0x09  /* open-drain                  */
#define GPIO_MODE_OUT_PULLUP_OD    0x0A

/* P0LPU pull-up strength */
#define P0LPU_RES_50K              0x00
#define P0LPU_RES_100K             0x10
#define P0LPU_RES_150K             0x20
#define P0LPU_RES_300K             0x30

/* ------------------------------------------------------------------ *
 * IAP / flash commands (datasheet §3.1.5.4)
 *
 * IAP_CMDL must be the bitwise complement of IAP_CMDH or the operation locks
 * and fails. The 16-bit constants below encode the pair as CMDH:CMDL.
 * ------------------------------------------------------------------ */

#define IAP_CMD_UNLOCK          0xF00F
#define IAP_CMD_ERASE_SECTOR    0xD22D  /* 128-byte sector; any address in it */
#define IAP_CMD_PROGRAM_BYTE    0xB44B
#define IAP_CMD_TRIGGER         0xE11E
#define IAP_CMD_RESET_KEEPOPT   0x8778  /* soft reset, keep options           */
#define IAP_CMD_RESET_REREADOPT 0x7887  /* soft reset, re-read options        */
#define IAP_CMD_ERASE_ALL       0xC33C  /* all-block erase; clears FLASH_SC.
                                         * Absent from the datasheet, named
                                         * ForAllBlock in HC89S003AF4.dev.
                                         * UNVERIFIED from user code.         */

/*
 * OPEN QUESTION — byte order within a 16-bit IAP_CMD write.
 *
 * Keil C51 can declare `sfr16 IAP_CMD = 0xFC;` and the vendor-flavoured
 * smartpad library writes `IAP_CMD = 0xF00F;` as a single assignment. Which of
 * IAP_CMDH / IAP_CMDL that stores first is NOT documented, and the chip's
 * complement interlock means the order plausibly matters — the natural hardware
 * design latches on the write that completes the pair.
 *
 * This macro writes CMDH then CMDL, because the datasheet's own command table
 * is ordered CMDH | CMDL and a latch-on-CMDL design is the likelier one. It is
 * a reasoned default, not a verified fact. If flash operations fail with every
 * other detail correct, try IAP_CMD_WRITE_LH() — that is the cheapest thing to
 * flip in the whole sequence.
 */
#define IAP_CMD_WRITE(v)    do { IAP_CMDH = (unsigned char)((v) >> 8); \
                                 IAP_CMDL = (unsigned char)(v); } while (0)
#define IAP_CMD_WRITE_LH(v) do { IAP_CMDL = (unsigned char)(v);        \
                                 IAP_CMDH = (unsigned char)((v) >> 8); } while (0)

/* IAP_ADDR is 14-bit, so the whole 16 KB flash is addressable. */
#define IAP_ADDR_WRITE(a)   do { IAP_ADDRH = (unsigned char)((a) >> 8); \
                                 IAP_ADDRL = (unsigned char)(a); } while (0)

/*
 * PORTING NOTE — `IAP_CMD` and `IAP_ADDR` are deliberately NOT defined as
 * assignable names, because controlling the byte order matters (above) and no
 * portable lvalue can express it. Keil-flavoured vendor code that writes
 * `IAP_CMD = 0xF00F;` must become `IAP_CMD_WRITE(IAP_CMD_UNLOCK);` under SDCC.
 * In practice that is four lines in smartpad_hc89s003/src/hc89s003_flash.c.
 *
 * Sequence rules, from §3.1.5.4 and confirmed against working vendor code:
 *   erase:    IAP_CMD=unlock -> IAP_ADDR=a -> IAP_CMD=D22D -> IAP_CMD=E11E
 *   program:  IAP_DATA=d FIRST, then unlock -> IAP_ADDR=a -> B44B -> E11E
 * No instruction may be inserted between unlock and trigger, so run the whole
 * sequence with interrupts disabled. After the trigger the part re-locks itself
 * and IAP_ADDR reverts to 0x3FFF. Reading flash back is a plain MOVC.
 */

/* ------------------------------------------------------------------ *
 * Flash geometry (datasheet §3.1)
 * ------------------------------------------------------------------ */

#define FLASH_SIZE              0x4000  /* 16 KB total                   */
#define FLASH_USER_ROM_SIZE     0x3000  /* 12 KB usable                  */
#define FLASH_SECTOR_SIZE       128     /* erase granularity             */
#define FLASH_PAGE_SIZE         1024    /* 8 sectors                     */
#define FLASH_PROTECT_UNIT_SIZE 4096    /* 4 pages; protection unit      */
#define EEPROM_SIZE             128

/* OPTION area offsets (datasheet §3.1.4). Reading these needs INSCON[IAPS]=1. */
#define OPTION_SN_DATA0         0x0000  /* ..0x0007, user data           */
#define OPTION_ID_DATA0         0x0008  /* ..0x000F, user data           */
#define OPTION_FLASH_SC0        0x0020  /* ..0x0023, 32-bit ICP password */
#define OPTION_ERST_ENB         0x0031
#define OPTION_WAIT_TS          0x0038
#define OPTION_BORVS            0x0039
#define OPTION_RVCFG            0x003E
#define OPTION_nRVCFG           0x003F  /* complement of RVCFG           */
#define OPTION_CHIP_ID0         0x0100  /* ..0x0107, factory, never erasable */

/* ------------------------------------------------------------------ *
 * Interrupt vector numbers for __interrupt / interrupt (datasheet §8.2)
 *
 * These are the "interrupt number (C Language)" column, not the vector
 * addresses. INT2..INT7 share one vector, as do INT8..INT15.
 * ------------------------------------------------------------------ */

#define INT0_VECTOR         0   /* 0x0003 */
#define T0_VECTOR           1   /* 0x000B */
#define INT1_VECTOR         2   /* 0x0013 */
#define T1_VECTOR           3   /* 0x001B */
#define UART1_VECTOR        4   /* 0x0023 */
#define WDT_VECTOR          5   /* 0x002B */
#define LVD_VECTOR          6   /* 0x0033 */
#define UART2_VECTOR        7   /* 0x003B */
#define SPI_VECTOR          8   /* 0x0043 */
#define T3_VECTOR           9   /* 0x004B */
#define T4_VECTOR          10   /* 0x0053 */
#define PWM_VECTOR         11   /* 0x005B, shared by PWM0..PWM3 */
#define T5_VECTOR          12   /* 0x0063 */
#define ADC_VECTOR         13   /* 0x006B */
#define INT2_7_VECTOR      14   /* 0x0073 */
#define INT8_15_VECTOR     15   /* 0x007B */
#define IIC_VECTOR         16   /* 0x0083 */

/* ------------------------------------------------------------------ *
 * Clock bring-up to 16 MHz, from the vendor's GPIO example
 *
 *   WDTCCR = 0x00;    close the watchdog
 *   CLKSWR = 0x51;    internal RC32M as system clock, /2 -> FOSC = 16 MHz
 *   CLKDIV = 0x01;    FCPU = FOSC / 1 = 16 MHz
 *
 * Power-on default is internal RC32M with FOSC 4 MHz and FCPU 2 MHz.
 * ------------------------------------------------------------------ */

#endif /* HC89S003F4_H */
