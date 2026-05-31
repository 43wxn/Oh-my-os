/* ============================================================================
 * kernel/kernel.cc — 内核主函数 (M4: 进程管理 + 交互式 Shell)
 * ============================================================================ */

#include "kernel/printk.h"
#include "kernel/arch/x86/idt.h"
#include "kernel/arch/x86/pic.h"
#include "kernel/arch/x86/pit.h"
#include "kernel/arch/x86/paging.h"
#include "kernel/arch/x86/port.h"
#include "kernel/mm/pmm.h"
#include "kernel/mm/vmm.h"
#include "kernel/mm/heap.h"
#include "kernel/drivers/keyboard/keyboard.h"
#include "kernel/proc/proc.h"

extern volatile uint32_t jiffies;
extern void clear_screen();

/* ── 简易字符串函数 (无 libc) ── */
static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ── 键盘 + 回显: 从队列读一个字符, 处理回显/退格 ── */
static char readchar() {
    char c = kbd_getchar();
    if (c == '\b') {
        putbackspace();
    } else if (c == '\t') {
        putchar(' ');
        putchar(' ');
    } else if (c >= ' ' || c == '\n') {
        putchar(c);
    }
    return c;
}

/* ── 交互式 Shell ── */
static void shell_loop() {
    printk("\nOh-my-os Shell  (type 'help')\n");

    char cmd[128];
    int  pos = 0;

    for (;;) {
        putchar('>');
        putchar(' ');
        pos = 0;

        /* 读一行命令 */
        for (;;) {
            char c = readchar();
            if (c == '\n') break;
            if (c == '\b') {
                if (pos > 0) pos--;
                continue;
            }
            if (c >= ' ' && pos < 120) {
                cmd[pos++] = c;
            }
        }
        cmd[pos] = '\0';

        /* 空行跳过 */
        if (pos == 0) continue;

        /* ── 命令解析 ── */
        if (str_eq(cmd, "help")) {
            printk("\nCommands:\n");
            printk("  help   - show this\n");
            printk("  clear  - clear screen\n");
            printk("  info   - system info\n");
            printk("  echo X - print X\n");
            printk("  m4test - M4 scheduler test\n");
            printk("  crash  - trigger #PF (test exception)\n\n");
        }
        else if (str_eq(cmd, "clear")) {
            clear_screen();
            printk("Oh-my-os M4 Shell\n");
        }
        else if (str_eq(cmd, "info")) {
            printk("\n=== System Info ===\n");
            printk("  Jiffies: %d (%d sec)\n", jiffies, jiffies / 100);
            printk("  Paging:  %d\n", paging_is_enabled());
            printk("  Threads: %d max\n", 16);
            printk("\n");
        }
        else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' && cmd[4] == ' ') {
            printk("\n%s\n\n", cmd + 5);
        }
        else if (str_eq(cmd, "crash")) {
            printk("\nTriggering #PF...\n");
            *(volatile int *)0xDEADBEEF = 0;
        }
        else if (str_eq(cmd, "m4test")) {
            printk("\n=== M4 Scheduler Test ===\n");
            printk("Creating 3 threads A/B/C, each yields 5 times...\n\n");

            proc_create([]() {
                for (int i = 0; i < 5; i++) {
                    printk("[A:%d] ", i);
                    proc_yield();
                }
                printk("[A EXIT] ");
            });

            proc_create([]() {
                for (int i = 0; i < 5; i++) {
                    printk("[B:%d] ", i);
                    proc_yield();
                }
                printk("[B EXIT] ");
            });

            proc_create([]() {
                for (int i = 0; i < 5; i++) {
                    printk("[C:%d] ", i);
                    proc_yield();
                }
                printk("[C EXIT] ");
            });

            /* 给线程时间运行 (boot 被 PIT 抢占切换到测试线程) */
            for (volatile int i = 0; i < 10000000; i++) {
                __asm__ volatile ("pause");
            }

            printk("\n\nM4 test done. Output should show A/B/C interleaving.\n");
            printk("If so: yield + preempt + exit all work.\n\n");
        }
        else {
            printk("\n? %s  (type 'help')\n", cmd);
        }
    }
}

extern "C" void kernel_main() {
    uint16_t *vga = reinterpret_cast<uint16_t *>(0xB8000);
    for (int i = 0; i < 80 * 25; i++) {
        vga[i] = 0x0F20;
    }

    printk("Oh-my-os M4 Shell\n");
    printk("=================\n");

    idt_init();
    pic_init();
    pmm_init();
    vmm_init();
    heap_init();
    proc_init();

    static pcb boot_pcb;
    proc_set_current(&boot_pcb);
    keyboard_init();
    pit_init(100);

    printk("Init OK.  paging=%d  pit=100Hz  ticks=%d\n",
           paging_is_enabled(), jiffies);

    __asm__ volatile ("sti");
    shell_loop();
}
