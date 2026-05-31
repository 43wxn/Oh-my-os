/* ============================================================================
 * demo/boot.S — 最小可启动 MBR (实机优先版)
 *
 * 改进点:
 *   1. 直接写 VGA 显存，不依赖 BIOS INT 0x10 电传
 *   2. 包含激活分区表项 (某些 BIOS 拒绝无分区表的 MBR)
 *   3. 长跳转规范化 CS:IP
 * ============================================================================ */

    .code16
    .section .text
    .globl _start

_start:
    /* ── 0. 规范化 CS:IP ── */
    ljmp    $0x0000, $1f
1:

    /* ── 1. 设置段寄存器 ── */
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7BFC, %sp

    /* ── 2. 设置 VGA 文本模式 80×25 ── */
    movw    $0x0003, %ax
    int     $0x10

    /* ── 3. 直接写 VGA 显存 ── */
    movw    $0xB800, %ax
    movw    %ax, %es
    xorw    %di, %di
    leaw    msg, %si

2:
    lodsb
    testb   %al, %al
    jz      3f
    cmpb    $0x0A, %al               /* LF → 跳到下一行开头 */
    jne     .not_lf
    movw    %di, %ax
    movb    $160, %bl
    divb    %bl
    incw    %ax
    mulb    %bl
    movw    %ax, %di
    jmp     2b
.not_lf:
    cmpb    $0x0D, %al               /* CR → 跳过 */
    je      2b
    stosb                            /* 写字符 */
    movb    $0x0F, %al               /* 属性: 白字黑底 */
    stosb
    jmp     2b

3:
    cli
    hlt

/* ── 字符串 ── */
msg:
    .ascii  "Hello OS!"
    .byte   0x0A
    .ascii  "Oh-my-os v0.0.1"
    .byte   0x0A
    .ascii  "MBR at 0x7C00, VGA direct write."
    .byte   0x0A
    .byte   0

/* ── 填充到 446 ── */
    .fill   446 - (. - _start), 1, 0

/* ── 分区表 (offset 446) ── */
ptab:
    .byte   0x80                    /* 激活              */
    .byte   0x00, 0x01, 0x01       /* 起始 CHS          */
    .byte   0x0C                    /* FAT32 LBA         */
    .byte   0xFE, 0xFF, 0xFF       /* 结束 CHS          */
    .long   0x00000001              /* 起始 LBA = 1      */
    .long   0x00FFFFFF              /* 扇区数 (~8GB)     */

    .fill   16 * 3, 1, 0            /* 三个空分区表项    */

/* ── 填充到 510，放魔数 ── */
    .fill   510 - (. - _start), 1, 0
    .byte   0x55
    .byte   0xAA
