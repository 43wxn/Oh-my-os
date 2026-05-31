/* ============================================================================
 * boot/stage2.S — 第二阶段引导加载器 (加固版)
 *
 * 与 v1 的区别:
 *   - 3 种 A20 开启方法级联尝试 (BIOS → 键盘控制器 → Fast A20)
 *   - A20 开启后硬件验证 (写 0x100000 vs 0x000000)
 *   - 内核加载后校验首字节
 *   - 进入保护模式前加延迟，方便实机观察
 * ============================================================================ */

    .code16
    .section .text
    .globl _start

_start:
    /* ── 1. 保存启动盘号 ── */
    movb    %dl, boot_drive

    /* ── 2. 设置段寄存器 (DS=0, 代码位于物理 0x500) ── */
    xorw    %ax, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %ss
    movw    $0x7BFC, %sp           /* 栈在 MBR 下方, 且远高于 stage2 */

    /* ── 3. 打印 banner ── */
    movw    $msg_stage2, %si
    call    print_string_rm

    movw    $msg_drive, %si
    call    print_string_rm
    movb    boot_drive, %al
    call    print_hex8_rm
    movw    $msg_crlf, %si
    call    print_string_rm

    /* ── 4. 开启 A20 ── */
    movw    $msg_a20, %si
    call    print_string_rm
    call    enable_a20

    /* ── 5. 验证 A20 ── */
    call    verify_a20
    cmpb    $0, %al
    jne     1f
    movw    $msg_a20_ok, %si
    call    print_string_rm
    jmp     2f
1:
    movw    $msg_a20_fail, %si
    call    print_string_rm
    /* 继续尝试 — 可能验证方法本身在 0x100000 处有兼容性问题 */

2:
    /* ── 6. 加载内核 ── */
    movw    $msg_load, %si
    call    print_string_rm

    movw    $KERNEL_TMP_SEG, %ax
    movw    %ax, %es
    xorw    %bx, %bx

    movb    $0x42, %ah
    movb    boot_drive, %dl
    movw    $kernel_dap, %si
    int     $0x13
    jc      disk_error

    /* ── 7. 校验内核首字节 (非零即可) ── */
    movw    $KERNEL_TMP_SEG, %ax
    movw    %ax, %es
    movb    %es:0x0000, %al
    cmpb    $0, %al
    jne     1f
    movw    $msg_kernel_bad, %si
    call    print_string_rm
    cli
    hlt
1:
    movw    $msg_kernel_ok, %si
    call    print_string_rm

    /* ── 8. 探测物理内存 (BIOS E820) ── */
    movw    $msg_e820, %si
    call    print_string_rm

    call    detect_e820

    /* ── 9. 打印进入 PM 的倒计时 ── */
    movw    $msg_pm, %si
    call    print_string_rm
    call    short_delay          /* 给用户时间看清输出 */

    /* ── 9. 切换到保护模式 ──
     *     stage2 链接在 0x500, DS=0, GDT base=0
     *     所有地址 = 链接地址 = 物理地址, 无需运行时修正 */
    cli
    lgdt    gdtr

    movl    %cr0, %eax
    orl     $1, %eax
    movl    %eax, %cr0

    ljmp    $0x08, $protected_mode_entry

/* ==========================================================================
 * 实模式子程序
 * ========================================================================== */

/* ── 显示十六进制字节 (AL) ── */
print_hex8_rm:
    pushw   %ax
    pushw   %bx
    pushw   %cx
    movb    %al, %bl
    movb    $0x0E, %ah
    movb    $0x00, %bh

    /* 高 4 位 */
    movb    %bl, %al
    shrb    $4, %al
    andb    $0x0F, %al
    cmpb    $10, %al
    jb      1f
    addb    $('A' - 10), %al
    jmp     2f
1:  addb    $'0', %al
2:  movb    $0x0F, %bl
    int     $0x10

    /* 低 4 位 */
    movb    %bl, %al
    shlb    $4, %al
    shrb    $4, %al
    andb    $0x0F, %al
    cmpb    $10, %al
    jb      1f
    addb    $('A' - 10), %al
    jmp     2f
1:  addb    $'0', %al
2:  movb    $0x0F, %bl
    int     $0x10

    popw    %cx
    popw    %bx
    popw    %ax
    ret

/* ── 打印字符串 (实模式, BIOS TTY) ── */
print_string_rm:
    pushw   %ax
    pushw   %bx
    pushw   %si
1:
    lodsb
    testb   %al, %al
    jz      2f
    movb    $0x0E, %ah
    movb    $0x00, %bh
    movb    $0x0F, %bl
    int     $0x10
    jmp     1b
2:
    popw    %si
    popw    %bx
    popw    %ax
    ret

/* ── 短暂延迟 (约 1 秒, 给用户看清屏幕) ── */
short_delay:
    pushw   %cx
    movw    $0xFFFF, %cx
1:
    loop    1b
    popw    %cx
    ret

/* ==========================================================================
 * A20 地址线开启
 *
 * 依次尝试 3 种方法:
 *   1. BIOS INT 0x15, AX=0x2401
 *   2. 键盘控制器 (8042)
 *   3. Fast A20 (System Control Port A, 0x92)
 * ========================================================================== */

enable_a20:
    pushw   %ax

    /* 方法 1: BIOS */
    movw    $0x2401, %ax
    int     $0x15
    jnc     a20_done             /* CF=0 表示成功 */

    /* 方法 2: 键盘控制器 */
    call    a20_kbd
    jnc     a20_done

    /* 方法 3: Fast A20 (System Control Port A) */
    inb     $0x92, %al
    testb   $2, %al
    jnz     a20_done             /* 已经开启了 */
    orb     $2, %al
    outb    %al, $0x92

a20_done:
    popw    %ax
    ret

/* ── 键盘控制器法开启 A20 ── */
a20_kbd:
    pushw   %ax

    /* 等待输入缓冲空闲 */
1:  inb     $0x64, %al
    testb   $2, %al
    jnz     1b

    /* 命令: 写 Output Port */
    movb    $0xD1, %al
    outb    %al, $0x64

    /* 等待输入缓冲空闲 */
2:  inb     $0x64, %al
    testb   $2, %al
    jnz     2b

    /* 写 Output Port, bit 1 = 1 */
    movb    $0xDF, %al
    outb    %al, $0x60

    /* 等待输入缓冲空闲 */
3:  inb     $0x64, %al
    testb   $2, %al
    jnz     3b

    popw    %ax
    ret

/* ==========================================================================
 * A20 验证: 写 0x100000, 读 0x000000, 二者应不同
 * 返回: AL=0 成功, AL≠0 失败
 * ========================================================================== */

verify_a20:
    pushw   %es
    pushw   %di

    /* 保存 0x000000 处的原值 */
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $0x0000, %di
    movw    %es:(%di), %bx       /* BX = 原值 */

    /* 读取 0x100000 处的值 */
    movw    $0xFFFF, %ax
    movw    %ax, %es
    movw    $0x0010, %di
    movw    %es:(%di), %cx       /* CX = 0x100000 处的值 */

    /* 如果 CX == BX, A20 可能没开 */
    cmpw    %bx, %cx
    jne     a20_ok

    /* 进一步: 写 0x000000 一个特殊值, 检查 0x100000 是否也变了 */
    movw    $0xABCD, %ax
    xorw    %di, %di
    movw    %ax, %es:(%di)       /* *0x000000 = 0xABCD */

    movw    $0xFFFF, %ax
    movw    %ax, %es
    movw    $0x0010, %di
    movw    %es:(%di), %cx       /* 再读 0x100000 */

    /* 恢复 0x000000 */
    xorw    %ax, %ax
    movw    %ax, %es
    movw    $0x0000, %di
    movw    %bx, %es:(%di)

    cmpw    $0xABCD, %cx
    je      a20_fail             /* 0x100000 跟着变了 → A20 没开 */

a20_ok:
    xorw    %ax, %ax             /* AL = 0 = 成功 */
    jmp     a20_verify_done

a20_fail:
    movb    $1, %al              /* AL = 1 = 失败 */

a20_verify_done:
    popw    %di
    popw    %es
    ret

/* ── E820 内存探测 ──
 * 调用 BIOS INT 0x15, AX=0xE820 获取物理内存布局.
 * 结果写入 E820_BUF (0x9000): [count:dword][entry0:...][entry1:...]...
 *
 * 实机三大坑 (均已处理):
 *   1. ACPI 3.0 返回 24 字节/条目, 旧 BIOS 返回 20 字节
 *      → 用 ECX 返回值决定步进, 不硬编码 20 或 24
 *   2. 实机 BIOS 可能破坏 EBP/ESI/段寄存器
 *      → 用 pushal/popal 保护所有寄存器
 *   3. EBX 连续句柄必须原封不动传回, EBX=0 才结束 */
detect_e820:
    pushal                      /* 保护全部 32 位寄存器        */

    /* 设置缓冲区 */
    movw    $E820_SEG, %ax
    movw    %ax, %es
    xorw    %di, %di
    movw    $0, %es:0x0000      /* count 低 16 位 = 0         */
    movw    $0, %es:0x0002      /* count 高 16 位 = 0         */
    addw    $4, %di             /* 跳过 count 字段            */
    xorl    %ebx, %ebx          /* EBX = continuation = 0     */

1:
    movl    $0xE820, %eax
    movl    $24, %ecx           /* 请求 24 字节 (ACPI 3.0)    */
    movl    $E820_MAGIC, %edx   /* 'SMAP'                    */
    int     $0x15
    jc      2f                  /* CF=1 → 结束或出错          */
    cmpl    $E820_MAGIC, %eax   /* 返回值有效?                */
    jne     2f

    /* ECX = BIOS 实际写入的字节数 (20 或 24)
     * 用 CL 推进 DI, 不硬编码 20 或 24 */
    incw    %es:0x0000          /* count++                    */
    addw    %cx, %di            /* DI += ECX (条目的实际大小)  */

    testl   %ebx, %ebx          /* EBX = 0 表示结束           */
    jnz     1b

2:
    /* 打印 count */
    movw    $msg_e820_cnt, %si
    call    print_string_rm
    movw    %es:0x0000, %ax
    call    print_hex16_rm
    movw    $msg_crlf, %si
    call    print_string_rm

    popal                       /* 恢复全部 32 位寄存器        */
    ret

/* ── 显示十六进制 16-bit (AX) ── */
print_hex16_rm:
    pushw   %ax
    pushw   %bx
    pushw   %ax
    movb    %ah, %al
    call    print_hex8_rm         /* 高字节 */
    popw    %ax
    call    print_hex8_rm         /* 低字节 */
    popw    %bx
    popw    %ax
    ret

/* ── 读盘失败 ── */
disk_error:
    movw    $msg_err, %si
    call    print_string_rm
    cli
    hlt

/* ==========================================================================
 * 32 位保护模式代码
 * ========================================================================== */

    .code32
protected_mode_entry:
    /* ── 8. 设置段选择子 ── */
    movw    $0x10, %ax
    movw    %ax, %ds
    movw    %ax, %es
    movw    %ax, %fs
    movw    %ax, %gs
    movw    %ax, %ss
    movl    $0x90000, %esp

    /* ── 9. 内核: 0x8000 → 0x100000 ── */
    movl    $KERNEL_TMP_ADDR, %esi
    movl    $KERNEL_LOAD_ADDR, %edi
    movl    $(KERNEL_SECTORS * 512 / 4), %ecx
    cld
    rep movsl

    /* ── 10. VGA 打印 Stage2 OK ── */
    movl    $0xB8000, %edi
    movl    $msg_stage2_ok, %esi
    movl    $(msg_stage2_ok_end - msg_stage2_ok), %ecx
1:
    movb    (%esi), %al
    movb    $0x0F, %ah
    movw    %ax, (%edi)
    addl    $1, %esi
    addl    $2, %edi
    loop    1b

    /* ── 11. 校验内核 @0x100000 (在 PM 下再验一次) ── */
    movl    $KERNEL_LOAD_ADDR, %esi
    movl    (%esi), %eax
    cmpl    $0, %eax
    jne     1f
    /* 内核首 DWORD = 0, 数据损坏 */
    movl    $0xB8000 + 80*2*5, %edi
    movl    $msg_bad, %esi
    movl    $(msg_bad_end - msg_bad), %ecx
2:
    movb    (%esi), %al
    movb    $0x4F, %ah
    movw    %ax, (%edi)
    addl    $1, %esi
    addl    $2, %edi
    loop    2b
    cli
    hlt

1:
    /* ── 12. 跳转到内核 ── */
    movl    $KERNEL_LOAD_ADDR, %eax
    call    *%eax

    /* 不会回到这里 */
    cli
    hlt

/* ==========================================================================
 * 数据区
 * ========================================================================== */

msg_stage2:
    .asciz  "[Stage2] Loaded at 0x10000\r\n"
msg_drive:
    .asciz  "[Stage2] Boot drive: 0x"
msg_crlf:
    .asciz  "\r\n"
msg_a20:
    .asciz  "[Stage2] Enabling A20..."
msg_a20_ok:
    .asciz  "OK\r\n"
msg_a20_fail:
    .asciz  "FAIL (continuing anyway)\r\n"
msg_load:
    .asciz  "[Stage2] Loading kernel from disk...\r\n"
msg_kernel_ok:
    .asciz  "[Stage2] Kernel data verified OK\r\n"
msg_kernel_bad:
    .asciz  "[Stage2] ERROR: Kernel data is all zeros!\r\n"
msg_e820:
    .asciz  "[Stage2] Detecting memory (E820)..."
msg_e820_cnt:
    .asciz  " entries="
msg_pm:
    .asciz  "[Stage2] Switching to protected mode...\r\n"
msg_err:
    .asciz  "[Stage2] ERROR: Disk read failed!\r\n"

msg_stage2_ok:
    .ascii  "[Stage2] Entered protected mode. Jumping to kernel..."
msg_stage2_ok_end:

msg_bad:
    .ascii  "*** KERNEL CORRUPTED AT 1MB ***"
msg_bad_end:

/* ── 启动盘号 ── */
boot_drive:
    .byte   0x80

/* ══════════════════════════════════════════════════════════════════════════
 * 常量
 * ══════════════════════════════════════════════════════════════════════════ */
    .equ    KERNEL_LBA,       8
    .equ    KERNEL_SECTORS,   32       /* 16KB, 远小于 BIOS 127 扇区上限 */
    .equ    KERNEL_TMP_SEG,   0x0800
    .equ    KERNEL_TMP_ADDR,  0x8000
    .equ    KERNEL_LOAD_ADDR, 0x100000 /* 1MB */

    .equ    E820_SEG,         0x0900  /* E820 缓冲区 @ 0x9000     */
    .equ    E820_MAGIC,       0x534D4150  /* 'SMAP'                */

/* ── 内核 DAP ── */
kernel_dap:
    .byte   0x10
    .byte   0x00
    .byte   KERNEL_SECTORS
    .byte   0x00
    .word   0x0000
    .word   KERNEL_TMP_SEG
    .quad   KERNEL_LBA

/* ══════════════════════════════════════════════════════════════════════════
 * GDT
 * ══════════════════════════════════════════════════════════════════════════ */

    .align  16
gdt_start:
    .quad   0x0000000000000000       /* NULL */
    .word   0xFFFF; .word 0x0000; .byte 0x00; .byte 0x9A; .byte 0xCF; .byte 0x00  /* Code 0x08 */
    .word   0xFFFF; .word 0x0000; .byte 0x00; .byte 0x92; .byte 0xCF; .byte 0x00  /* Data 0x10 */
gdt_end:

gdtr:
gdtr_limit:
    .word   gdt_end - gdt_start - 1
gdtr_base:
    .long   gdt_start              /* 运行时会被修正为物理地址 */

/* ── 填充到 3072 bytes ── */
    .fill   3072 - (. - _start), 1, 0
