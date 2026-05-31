; ============================================================================
; demo/boot.asm — 最小可启动 MBR
;
; 编译: nasm -f bin boot.asm -o boot.bin
; QEMU: qemu-system-i386 -drive file=boot.bin,format=raw
; 实机: sudo dd if=boot.bin of=/dev/sdX bs=512 count=1
;
; 目标: 512 字节 MBR → VGA 输出 "Hello OS!" → 证明工具链 & 硬件链路通
; ============================================================================

[bits 16]
[org 0x7C00]

start:
    ; ── 1. 设置段寄存器 ──
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; 栈向 0x7C00 以下生长

    ; ── 2. 设置 VGA 文本模式 80×25 (BIOS int 0x10) ──
    mov ax, 0x0003
    int 0x10

    ; ── 3. 打印字符串 ──
    mov si, msg

.loop:
    lodsb                   ; al = [si++]
    test al, al
    jz .done

    ; BIOS 电传输出 (int 0x10, ah=0x0E)
    mov ah, 0x0E
    mov bh, 0x00            ; page 0
    mov bl, 0x0F            ; 白色
    int 0x10
    jmp .loop

.done:
    ; ── 4. 暂停 ──
    cli
    hlt

; ── 数据 ──
msg:
    db 0x0D, 0x0A          ; 回车换行
    db "Hello OS!", 0x0D, 0x0A
    db 0x0D, 0x0A
    db "Oh-my-os booting from real hardware.", 0x0D, 0x0A
    db "Toolchain & hardware link verified.", 0x0D, 0x0A
    db 0

; ── 填充到 510 字节，末尾 0x55AA ──
times 510-($-$$) db 0
dw 0xAA55
