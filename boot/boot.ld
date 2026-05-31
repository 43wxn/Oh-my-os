/* boot/boot.ld — Stage2 链接脚本
 *
 * Stage2 加载到物理 0x500, 段寄存器 DS=0
 * 链接基址 = 物理地址 = 0x500
 * 所有 label 值就是物理地址, 不复用段偏移
 */

OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x0500;

    .text : {
        *(.text)
    }

    .data : {
        *(.data)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}
