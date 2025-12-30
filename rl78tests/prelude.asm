REG_X       .SET    0
REG_A       .SET    1
REG_C       .SET    2 
REG_B       .SET    3
REG_E       .SET    4
REG_D       .SET    5
REG_L       .SET    6
REG_H       .SET    7
REG_PSW     .SET    8

; Utility Macros

TEST_HEADER .MACRO
_TESTNO .SET    0
.text   .CSEG   TEXT
start:
.ENDM

TEST_FOOTER .MACRO
test_success:
    MOV A, #0
    BR  !test_success
test_fail:
    BR  !test_fail
.ENDM

TEST    .MACRO
_TESTNO  .SET    (_TESTNO + 1)
_ASSERTNO .SET  0
    MOV X, #0
    MOV A, #0
    MOV C, #0
    MOV B, #0
    MOV E, #0
    MOV D, #0
    MOV L, #0
    MOV H, #0
    MOV PSW, A
.ENDM

ARRANGE .MACRO
.ENDM

ACT .MACRO
.ENDM

ASSERT    .MACRO
    MOV !status_storage + REG_A, A
    MOV A, X
    MOV !status_storage + REG_X, A
    MOV A, C
    MOV !status_storage + REG_C, A
    MOV A, B
    MOV !status_storage + REG_B, A
    MOV A, E
    MOV !status_storage + REG_E, A
    MOV A, D
    MOV !status_storage + REG_D, A
    MOV A, L
    MOV !status_storage + REG_L, A
    MOV A, H
    MOV !status_storage + REG_H, A
    MOV A, PSW
    MOV !status_storage + REG_PSW, A
.ENDM

ASSERT_REG  .MACRO  REGNAME, VALUE
_ASSERTNO    .SET    (_ASSERTNO + 1)
    MOV A, !(status_storage + REGNAME)
    CMP A, #VALUE
    MOV A, #_TESTNO
    MOV X, #_ASSERTNO
    SKZ
    BR  !test_fail
.ENDM

ASSERT_REGS  .MACRO  VAL_X, VAL_A, VAL_C, VAL_B, VAL_E, VAL_D, VAL_L, VAL_H
    ASSERT_REG  REG_X, VAL_X
    ASSERT_REG  REG_A, VAL_A
    ASSERT_REG  REG_C, VAL_C
    ASSERT_REG  REG_B, VAL_B
    ASSERT_REG  REG_E, VAL_E
    ASSERT_REG  REG_D, VAL_D
    ASSERT_REG  REG_L, VAL_L
    ASSERT_REG  REG_H, VAL_H
.ENDM

; Sections
start   .VECTOR   0x00000

.SECTION    .stack_area,     BSS_AT 0xFFAE0
stack_area:
    .DS 1024    

.bss    .DSEG   BSS
status_storage:
    .DS 8           ; for registers
    .DS 1           ; for PSW
    .ALIGN 2
    .DS 2           ; for SP

.SECTION    .dataR, DATA 
.SECTION    .sdataR, SDATA
.SECTION    .data, DATA
.L_section_data:
.SECTION    .sdata, SDATA
.L_section_sdata:
