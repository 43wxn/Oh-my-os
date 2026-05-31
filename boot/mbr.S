/* ============================================================================
 * boot/mbr.S — MBR 引导扇区 (512 bytes)
 *
 * 职责:
 *   1. BIOS 加载到 0x7C00
 *   2. 从磁盘加载 Stage2 (sectors 1-7) 到 0x1000
 *   3. 跳转到 Stage2
 *
 * 磁盘布局:
 *   Sector 0:   MBR (本文件, 512B)
 *   Sector 1-7: Stage2 (boot/stage2.S, 最多 3584 bytes)
 *   Sector 8+:  内核扁平二进制
 * ============================================================================ */

    .code16
    .section .text
    .globl _start

_start:
    /* ── 1. 规范化 CS:IP ── */
    ljmp    $0x0000, $1f
1:

    /* ── 2. 设置段寄存器 ── */
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7BFC, %sp          /* 栈在 MBR 下方 */

    /* ── 3. 保存 BIOS 传递的启动盘号 ── */
    movb    %dl, boot_drive       /* DL = 启动盘号 (0x80 = 第一块硬盘) */

    /* ── 4. 从磁盘加载 Stage2 ── */
    /*     Stage2 位于 LBA 1-7 (共 7 个扇区) */
    movw    $STAGE2_SEG, %ax
    movw    %ax, %es              /* ES:BX = 目标地址 0x1000:0x0000 */
    xorw    %bx, %bx

    /* INT 0x13, AH=0x42 (Extended Read) */
    movb    $0x42, %ah
    movb    boot_drive, %dl       /* 启动盘号 */
    movw    $dap, %si             /* DS:SI = DAP 结构地址 */
    int     $0x13
    jc      disk_error            /* CF=1 表示读盘失败 */

    /* ── 5. 跳转到 Stage2 ── */
    ljmp    $STAGE2_SEG, $0x0000

/* ── 读盘失败处理 ── */
disk_error:
    movw    $0xB800, %ax
    movw    %ax, %es
    movw    $0x0000, %di
    movb    $'E', %al
    stosb
    movb    $0x4F, %al            /* 红底白字 */
    stosb
    movb    $'R', %al
    stosb
    movb    $0x4F, %al
    stosb
    movb    $'R', %al
    stosb
    movb    $0x4F, %al
    stosb
    cli
    hlt

/* ── 常量 ── */
    .equ    STAGE2_SEG, 0x0050    /* Stage2 @ 0x0050:0x0000 (= 物理 0x500)   */
    .equ    STAGE2_LBA, 1         /* Stage2 起始扇区 (LBA) */
    .equ    STAGE2_CNT, 7         /* Stage2 扇区数 (7 * 512 = 3584 bytes) */

/* ── DAP (Disk Address Packet) 结构 ── */
dap:
    .byte   0x10                  /* DAP 大小 (16 bytes) */
    .byte   0x00                  /* 保留 */
    .byte   STAGE2_CNT            /* 扇区数 */
    .byte   0x00                  /* 保留 */
    .word   0x0000                /* 目标偏移 (由 ES:BX 提供) */
    .word   STAGE2_SEG            /* 目标段 */
    .quad   STAGE2_LBA            /* 起始 LBA (64-bit little-endian) */

/* ── 启动盘号 (运行时填充) ── */
boot_drive:
    .byte   0x80

/* ── 填充到 446 bytes ── */
    .fill   446 - (. - _start), 1, 0

/* ── 分区表 ── */
    .byte   0x80                    /* 激活标志 */
    .byte   0x00, 0x01, 0x01       /* 起始 CHS */
    .byte   0x0C                    /* FAT32 LBA */
    .byte   0xFE, 0xFF, 0xFF       /* 结束 CHS */
    .long   0x00000001              /* 起始 LBA = 1 */
    .long   0x00FFFFFF              /* 扇区数 */

    .fill   48, 1, 0                /* 3 个空分区表项 */

/* ── MBR 魔数 ── */
    .byte   0x55
    .byte   0xAA
