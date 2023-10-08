#include <clock.h>
#include <console.h>
#include <defs.h>
#include <fdt.h>
#include <intr.h>
#include <kdebug.h>
#include <kmonitor.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>

int kern_init(uint32_t hartid, uintptr_t dtb_pa) __attribute__((noreturn));
void grade_backtrace(void);

int kern_init(uint32_t hartid, uintptr_t dtb_pa) {
    extern char edata[], end[];
    memset(edata, 0, end - edata);
    cons_init();  // init the console
    const char* message = "(NKU.osLoongTea) os is loading ...\0";
    // cprintf("%s\n\n", message);
    cputs(message);

    print_kerninfo();

    cprintf("hartid: %d\n", hartid);
    cprintf("dtb_pa: 0x%016lx\n", dtb_pa);

    // 0x80000000 is still mapped to itself, so just use the physical address
    // here.
    fdt_header_t* fdt_header = (fdt_header_t*)(dtb_pa);

    cprintf("fdt_magic:             0x%08x\n", le2be(fdt_header->magic));
    cprintf("fdt_totalsize:         0x%08x\n", le2be(fdt_header->totalsize));
    cprintf("fdt_off_dt_struct:     0x%08x\n",
            le2be(fdt_header->off_dt_struct));
    cprintf("fdt_off_dt_strings:    0x%08x\n",
            le2be(fdt_header->off_dt_strings));
    cprintf("fdt_off_mem_rsvmap:    0x%08x\n",
            le2be(fdt_header->off_mem_rsvmap));
    cprintf("fdt_version:           0x%08x\n", le2be(fdt_header->version));
    cprintf("fdt_last_comp_version: 0x%08x\n",
            le2be(fdt_header->last_comp_version));
    cprintf("fdt_boot_cpuid_phys:   0x%08x\n",
            le2be(fdt_header->boot_cpuid_phys));
    cprintf("fdt_size_dt_strings:   0x%08x\n",
            le2be(fdt_header->size_dt_strings));
    cprintf("fdt_size_dt_struct:    0x%08x\n",
            le2be(fdt_header->size_dt_struct));

    // Walk through the flattend device tree and print it out.
    // walk_print_device_tree(fdt_header);

    cprintf("\n");

    // grade_backtrace();
    idt_init();  // init interrupt descriptor table

    pmm_init();  // init physical memory management

    idt_init();  // init interrupt descriptor table

    clock_init();  // init clock interrupt

    intr_enable();  // enable irq interrupt

    /* do nothing */
    while (1)
        ;
}

void __attribute__((noinline))
grade_backtrace2(int arg0, int arg1, int arg2, int arg3) {
    mon_backtrace(0, NULL, NULL);
}

void __attribute__((noinline)) grade_backtrace1(int arg0, int arg1) {
    grade_backtrace2(arg0, (uintptr_t)&arg0, arg1, (uintptr_t)&arg1);
}

void __attribute__((noinline)) grade_backtrace0(int arg0, int arg1, int arg2) {
    grade_backtrace1(arg0, arg2);
}

void grade_backtrace(void) {
    grade_backtrace0(0, (uintptr_t)kern_init, 0xffff0000);
}

static void lab1_print_cur_status(void) {
    static int round = 0;
    round++;
}
