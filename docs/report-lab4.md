# 第四次实验报告

进程管理

## 进程控制块分配

`alloc_proc` 的实现如下，首先将所有的成员全部设置为 0，对应于指针则为 `NULL`，之后初始化状态为 `PROC_UNINIT`，`pid` 为 `-1`，页表基址设置为启动页表的地址

```c
static struct proc_struct *alloc_proc(void) {
    struct proc_struct *proc = kmalloc(sizeof(struct proc_struct));
    if (proc != NULL) {
        memset(proc, 0, sizeof(struct proc_struct));
        proc->state = PROC_UNINIT;
        proc->pid = -1;
        proc->cr3 = boot_cr3;
    }
    return proc;
}
```

1. `struct context context` 成员变量作用

   `context` 用于在进程切换的过程中保存这一进程的上下文。

2. `struct trapframe *tf` 成员变量作用

   用于进程在内核态切换时的上下文存储。

二者的差别可以从整个进程创建、启动的过程进行观察。首先，在 `proc_init` 中先手动初始化了 `idle` 这一内核线程，之后再通过 `kernel_thread` 创建了 `init` 这一内核线程并且输出 `Hello world!!`。

在 `kernel_thread` 中，首先设置了一个 `trapframe`。其中 `s0` 寄存器的位置存入函数地址，`s1` 的地址存入函数参数的地址，并且设置 `sstatus`。其中 `SPP` 位为 1 表示陷入这个 trap 前并非处于用户态。`SPIE` 表示陷入前开启了中断，`~SSTATUS_SIE` 表示关闭当前中断。之后 `epc` 被设置为了 `kernel_thread_entry` 地址。

之后调用 `do_fork`，并且指定拷贝内存，将 `tf` 的地址传入。`do_fork` 会先使用 `alloc_proc` 分配一个进程管理块。由于 `fork` 产生的进程一定是由父进程产生的，所以再将 `parent` 设置为 `current`，并且设置进程对应的内核栈空间（分配连续两个页的空间）。之后在 `copy_thread` 中会首先在新创建的进程对应的内核栈的顶部为 `trapframe` 设置空间，将传入的 `tf` 位置对应的内容（此处即为之前所设置的 `s0` `s1` `sepc` 以及 `sstatus`）拷贝到对应的位置；并且将返回值（`a0`）设置为 0 表示这是一个 `fork` 得到的子进程，并且由于传入的栈顶地址为 0，所以 `sp` 会指向 `proc->tf` 对应 `trapframe` 的底部。之后 `copy_thread` 还设置了进程对应的 `context` 的内容，将 `ra` 返回地址设置为了 `forkret`。

`forkret` 会调用 `forkrets` 这个汇编过程。`forkrets` 首先将 `sp` 设置为 `a0` 并且恢复 `trapframe` 中的所有的上下文，之后使用 `sret` 指令返回 `sepc` 对应的地址。所以在 `forkret` 中将参数设置为当前进程 `current` 的 `tf`。

之后回到 `do_fork` 中，ucore 初始化了 `pid` 并且维护了进程块在哈希表和链表中的位置，再调用 `wakeup_proc`，将进程状态设置为 `PROC_RUNNABLE`。至此完成了初始化工作。之后 ucore 会在 `cpu_idle` 中进行调度，`schedule` 会寻找 `PROC_RUNNABLE` 的进程，并且使用 `proc_run` 执行这一进程。`proc_run` 在设置页表之后会进行 `context` 的切换，并且最后返回到先前设置的 `ra` 的地址处继续执行。而由于设置内核线程时，`epc` 成员设置为了 `kernel_thread_entry` 所以此时 `__trapret` 会将跳转到 `kernel_thread_entry`，将此前设置在 `tf` 中的参数位置 `s1`（此时已写入寄存器）转移到 `a0` 作为实际的参数，并且跳转到 `s0` 对应的地址处，即 `kernel_thread` 调用时传入的 `fn`，此处即为 `init_main` 函数的地址。

至此，一个内核线程就成功被创建并且启动了。

在上述进程创建、调度的过程中，ucore 会首先让 CPU 执行 `context` 中设置的 `ra` 地址，但是此处仍然是处于 Supervisor Mode，此时就需要借助一个“伪造”的 `trapframe` 进行特权级的切换。对于内核线程来说，由于 `sstatus` 中的 `SPP` 位被设置，所以切换之后仍然为 S 态。

## 内核线程资源分配

编写的代码内容如下：

```c
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf) {
    /* ... checks ... */

    proc = alloc_proc();
    
    if (proc == NULL) {
        goto fork_out;
    }

    proc->parent = current;

    if (setup_kstack(proc) != 0) {
        goto bad_fork_cleanup_kstack;
    }

    if (copy_mm(clone_flags, proc) != 0) {
        goto bad_fork_cleanup_proc;
    }

    copy_thread(proc, stack, tf);

    bool intr_flag;
    local_intr_save(intr_flag);
    
    proc->pid = get_pid();
    hash_proc(proc);
    list_add(&proc_list, &(proc->list_link));
    nr_process++;

    local_intr_restore(intr_flag);

    wakeup_proc(proc);

    ret = proc->pid;

    /* ... cleanup ... */
}
```

首先使用 `alloc_proc` 分配一个进程块，使用 `setup_kstack` 设置这个进程在内核栈上对应的空间（两个页）。之后将内存进行拷贝（此实验中没有实现），并且复制上下文。再之后由于会对全局的资源进行改动，所以先关闭中断，分配 pid，将进程块链入哈希表和进程链表。最后恢复中断，设置运行状态为 `PROC_RUNNABLE`，并且返回进程对应的 pid。

ucore 分配进程 pid 的代码位于 `get_pid` 函数。注意到其中的 `last_pid` 和 `next_safe` 都是静态变量。ucore 会为每一个 fork 得到的进程分配一个唯一的 pid。`get_pid` 所做的工作可以总结为维护可用 pid 的上界 `next_safe`，从 `last_pid` 到 `next_safe`（开区间）能够保证为可用的 pid 号。如果没有找到这样的区间则不断在 `repeat` 中循环进行查找。

## `proc_run` 函数

`proc_run` 的实现如下：

```c
void proc_run(struct proc_struct *proc) {
    if (proc != current) {
        bool intr_flag;
        local_intr_save(intr_flag);

        struct proc_struct *prev = current;
        struct proc_struct *next = proc;

        current = proc;
        lcr3(proc->cr3);
        switch_to(&(prev->context), &(next->context));

        local_intr_restore(intr_flag);
    }
}
```

由于 `switch_to` 之后会跳转到 `trapret` 函数，而 `trapret` 会使用 `current->trap`，所以需要先记录，之后设置当前进程、页表地址并且切换上下文。由于此处在 `do_exit` 时会直接调用 `panic` 并且 `kmonitor` 中调用了 `sbi_shutdown` 所以操作系统在执行完 `init_main` 之后会直接关机。

在本实验中，一共创建了两个内核线程，一个为 `idle` 另外一个为执行 `init_main` 的 `init` 线程。 

## 中断开关的实现

首先 `local_intr_save` 会保存当前中断是否打开，`local_intr_restore` 时会根据保存的结果设置 `sstatus` 的 `SIE` 位从而实现开关中断的保存、关闭和重新开启。

