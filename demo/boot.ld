/* demo/boot.ld — MBR 链接脚本 */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    . = 0x7C00;

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
