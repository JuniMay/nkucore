# 第三次实验报告

缺页异常与页面置换。

## FIFO 页面替换算法

首先简述一下整个虚拟内存管理的过程。

当发生缺页异常时，出现异常的地址会传入 `do_pgfault` 函数进行缺页的处理。此时使用 `get_pte` 函数获取/创建对应虚拟地址映射过程中的页表和页表项：

1. 若得到的页表项为 0，表示这个页从来没有被分配，此时使用 `pgdir_alloc_page` 分配物理页并且使用 `page_insert` 建立映射。此外注意到在 `alloc_pages` 中增加了一个循环，当页面数量不够时使用 `swap_out` 函数将对应数量的页面换出。在 `swap_out` 中会调用页面置换管理器的 `swap_out_victim` 按照对应的策略换出页面，并且使用 `swapfs_write` 将页面内容写入“硬盘”中。
2. 若得到的页表项不为 0，表示这个地址对应的页是在之前被换出的（关于页表项值的操作见后文对 `swap_out` 具体操作的分析）。此时需要调用 `swap_in` 将页面内容从“硬盘”中通过 `swapfs_read` 读入对应的页面，并且使用 `page_insert` 将虚拟地址和物理页面的映射关系写入对应的页表项中。之后再使用 `swap_map_swappable` 调用不同策略的 `map_swappable` 进行维护，从而保证页面置换能够正确执行。

上述的操作对应于 ucore 虚拟内存管理过程中的功能实现，而具体的策略则由不同的 `swap_manager` 内的实现决定。现在分析 FIFO 页面替换算法中页面换入到被换出过程中的十个函数。

1. `do_pgfault`：整个缺页处理流程的开始，根据 `get_pte` 得到的页表项的内容确定页面是需要创建还是需要换入。
2. `get_pte`：根据触发缺页异常的虚拟地址查找其对应的多集页表叶节点的页表项。如果其中某一级页表不存在则为其分配一个新的页（4KiB）用于存储映射关系。
3. `swap_in`：将对应的页面根据存储在页表项位置的 `swap_entry_t` 从“硬盘”中读取页面内容，并且重新写入页面的内存区域。
4. `alloc_page`/`alloc_pages`：前者为后者的一个宏，会分配一个页面，如果不能分配则使用 `swap_out` 换出所需的页面数量。
5. `page_insert`：将虚拟地址和新分配的页面的物理地址在页表内建立一个映射。
6. `swap_map_swappable`：在使用 FIFO 页面替换算法时会直接调用 `_fifo_map_swappable`，这会将新加入的页面存入 FIFO 算法所需要维护的队列（使用链表实现）的开头从而保证先进先出的实现。
7. `swap_out`：根据需要换出的页面数量换出页面到“硬盘”中。
8. `sm->swap_out_victim()`：在 FIFO 中会直接调用 `_fifo_swap_out_victim` 进行页面换出，这会将链表最后（最先进入的页面）指定为需要被换出的页面。
9. `free_page`：将被换出的页面对应的物理页释放从而重新尝试分配。
10. `tlb_invalidate`：在页面换出之后刷新 TLB，防止地址映射和访问发生错误。

## 不同分页模式的工作原理

### 两段相似的代码

RISC-V 中目前共有四种分页模式 Sv32，Sv39，Sv48，Sv57。其中 Sv32 使用了二级页表，Sv39 使用了三级页表，Sv48 和 Sv57 分别使用了四级和五级页表。

`get_pte` 的目的是查找/创建特定线性地址对应的页表项。由于 ucore 使用 Sv39 的分页机制，所以共使用了三级页表，`get_pte` 中的两段相似的代码分别对应于在第一、第二级页表（PDX1，PDX2）的查找/创建过程。首先 `get_pte` 函数接收一个参数 `create` 标识未找到时是否要分配新页表项。之后首先通过 `PDX1` 宏获取线性地址 `la` 对应的第一级 `VPN2` 的值，并且在 `pgdir` 即根页表（页目录）中找到对应的地址，并且判断其 `V` 标志位是否为真（表示可用）。若可用则继续寻找下一级目录，若不可用则根据 `create` 的值决定是否调用 `alloc_page()` 开辟一个新的下一级页表，之后设置这一级页表的页表项，页面的引用以及下一级页表的页号的对应关系。之后再对下一级页表进行同样的操作，其中通过 `PDE_ADDR` 得到页表项对应的物理地址。最后根据得到的 `pdep0` 的页表项找到最低一级页表项的内容并且返回。

整个 `get_pte` 会对 Sv39 中的高两级页目录进行查找/创建以及初始化的操作，并且返回对应的最低一级页表项的内容。两段相似的代码分别对应了对不同级别 `VPN` 的操作。

### 查找和分配的功能

我认为这种查找和分配合并在一个函数内的写法并不好。如果只是考虑 Sv39 分页机制，这种写法能够实现功能，但是如果考虑分页机制的可扩展性，或许可以将创建某一级页表的代码独立成一个模块，在查找中调用，从而增强代码的复用，减少可能出现的错误。

## 物理页面映射

### `do_pgfault` 的实现过程

首先 `do_pgfault` 中会使用 `find_vma` 根据异常出现的地址在对应的 `mm` 中找到一个地址所对应的 `vma`。

`find_vma` 会优先查找 `mm_struct` 中的 `mmap_cache`缓存，若地址不在开始和结束范围内再遍历 `mmap_list` 查找对应的 `vma`。

之后根据 `vm_flags` 是否可写设定页表项的权限 `perm`，并且在 `mm` 对应的页目录中查找出现异常的地址的页表项。若查找到的页表项为 0，说明 `get_pte` 新建并且初始化了一个一级页表，此时需要使用 `pgdir_alloc_page` 为其重新分配页面。

之后的代码为需要编程实现的部分。若查找到的页表项并不是 0，则当前 `ptep` 地址上存储的是一个 `swap_entry_t` 表示一个已经被换到“硬盘”上的页，此时需要使用 `swap_in` 将页面换入，并且将换入的页面使用 `page_insert` 在页表中建立与物理地址的映射，并且使用 `swap_map_swappable` 调用 `swap_manager` 中的对应函数以维护页面置换管理。最后，需要将 `page->pra_vaddr` 设置为 `addr`。实现代码如下：

```c
int do_pgfault(struct mm_struct *mm, uint_t error_code, uintptr_t addr) {
    /* ... getting ptep ... */
    if (*ptep == 0) {
        /* ... pgdir_alloc_page ... */
    } else {
        if (swap_init_ok) {
            struct Page *page = NULL;
            // swap in the page from 'disk'
            int r = swap_in(mm, addr, &page);
            if (r != 0) {
                cprintf("swap_in in do_pgfault failed\n");
                goto failed;
            }
						// establish mapping in the page table.
            r = page_insert(mm->pgdir, page, addr, perm);
            if (r != 0) {
                cprintf("page_insert in do_pgfault failed\n");
                goto failed;
            }
						// maintain the swap manager.
            swap_map_swappable(mm, addr, page, 1);
            // set the `pra_vaddr`
            page->pra_vaddr = addr;
        } else {
            /* ... failed ... */
        }
    }
    /* ... return ... */
}
```

### 页目录项和页表项组成部分对 ucore 页替换算法的潜在用处

以下说明中将页目录项和页表项统称为页表项。Sv39 下一个页表项的组成如下

```
+---+----+---------+-------+-------+-------+---+---+---+---+---+---+---+---+---+
| N |PBMT| Reserved| PPN[2]| PPN[1]| PPN[0]|RSW| D | A | G | U | X | W | R | V |
+---+----+---------+-------+-------+-------+---+---+---+---+---+---+---+---+---+
  1    2      7       26       9       9     2   1   1   1   1   1   1   1   1  
```

低八位用于表示页表项的属性和权限。其中 `V` 标志位表示整个页表项是否合法，若 `V` 为 0，则整个页表项中的其余比特均无意义，并且可以由软件自由定义使用方式

> The V bit indicates whether the PTE is valid; if it is 0, all other bits in the PTE are don’t-cares and may be used freely by software.[^1]

[^1]: *The RISC-V Instruction Set Manual, Volume II: Privileged Architecture*, Document Version 20211203, p84

ucore 中页替换算法利用了这个 RISC-V 特权级的约定。当一个页面被换出时，页表项位置的值会被如下设置：

```c
*ptep = (page->pra_vaddr / PGSIZE + 1) << 8;
```

这会将对应页的虚拟页号加一并左移，保证其低八位为 0，并且整个值不为 0（表示是换出而不是被新建）。在 ucore 的实现中，一个被换出的页面中存储的内容约定为一个 `swap_entry_t` 类型的值，这个类型的值需要保证最低位即 `V` 标志位为 0，从而保证硬件访问这个页表项时会产生缺页异常。

通过在页表项中的标记，可以在 `do_pgfault` 时正确决定是需要为一个地址重新分配页并且建立映射关系还是从“硬盘”中换入页面。

在 sv39 中，页表项中的一些标记位和保留位（RSW）可以在页替换算法中用于实现一些功能。例如，A (Accessed) 可以表示一个页是否被访问（写或读）过，D (Dirty) 表示页是否被修改（写入）过，这两个标记位可以用于实现扩展的 Clock 页面置换算法。

### 缺页服务例程中的页访问异常

当出现页访问异常时，硬件需要根据发生异常的类型设置 `scause` 寄存器，将产生异常的指令地址存入 `sepc` 寄存器，将访问的地址存入 `stval`，并且根据设置的 `stvec` 进入操作系统的异常处理过程。之后再次调用操作系统的缺页服务例程进行缺页处理，从而陷入死循环。

### Page 与页目录项、页表项的对应关系

Sv39 分页机制下，每一个页表所占用的空间刚好为一个页的大小。在处理缺页时，如果一个虚拟地址对应的二级、三级页表项（页目录项）不存在，则会为其分配一个页，当第一级页表项没有设置过时也会分配一个页。此外，当一个页面被换出时，他所对应的页面会被释放，当一个页面被换入或者新建时，会分配一个页面。所以，对于本实验中缺页机制所处理和分配的所有页目录项、页表项，都对应于 `pages` 数组中的一个页，但是 `pages` 中的页并不一定会全部使用。

## Clock 页面替换算法

Clock 页面替换算法需要一个额外的 `curr_ptr` 指针来指向当前时钟指针的位置。

初始化时，除了和 FIFO 算法一样初始化 `pra_list_head` 和 `mm->sm_priv` 之外，还需要将 `curr_ptr` 指向链表的开头。

```c
static int _clock_init_mm(struct mm_struct *mm) {     
    list_init(&pra_list_head); // 初始化pra_list_head为空链表
    curr_ptr = &pra_list_head; // 初始化curr_ptr指向链表开头
    mm->sm_priv = &pra_list_head; // 将mm->sm_priv指向pra_list_head
    return 0;
}
```

每次 `map_swappable` 时，需要将新加入的页面插入到链表的开头，并且将页的 `visited` 设置为 1。

```c
static int _clock_map_swappable(struct mm_struct *mm, uintptr_t addr, struct Page *page, int swap_in) {
    list_entry_t *entry = &(page->pra_page_link);
    assert(entry != NULL && curr_ptr != NULL);
    list_add_before(mm->sm_priv, entry); // 将页面插入到链表尾
    page->visited = 1; // 将页面的visited标志置为1
    return 0;
}
```

当需要换出页面时，需要从当前 `curr_ptr` 开始遍历链表，查找第一个 `visited` 为 0 的页面，将该页面从页面链表中删除并换出，同时将遇到 `visited` 为 1 的页面的 `visited` 设置为 0。

```c
static int _clock_swap_out_victim(struct mm_struct *mm, struct Page ** ptr_page, int in_tick) {
    list_entry_t *head=(list_entry_t*) mm->sm_priv;
    assert(head != NULL);
    assert(in_tick==0);
    while (1) {
        if (curr_ptr == &pra_list_head) { // 循环遍历链表，跳过头节点
            curr_ptr = list_next(curr_ptr);
        }
        struct Page *p = le2page(curr_ptr, pra_page_link); // 获取当前页面对应的Page结构指针
        if (p->visited == 0) { // 如果当前页面未被访问
            *ptr_page = p; // 将该页面指针赋值给ptr_page作为换出页面
            cprintf("curr_ptr %p\n",curr_ptr);
            curr_ptr = list_next(curr_ptr);
            list_del(list_prev(curr_ptr)); // 将该页面从页面链表中删除
            break;
        } else { // 如果当前页面已被访问
            p->visited = 0;  // 将visited标志置为0，表示该页面已被重新访问
            curr_ptr = list_next(curr_ptr); // 继续遍历
        }
    }
    return 0;
}
```

### 比较Clock页替换算法和FIFO算法的不同

Clock 页面替换算法考虑了访问的情况，通过维护一个循环队列和一个指针，每次换出时寻找（近似）最早的未被访问的页面。而 FIFO 只是换出最早的页面。相比于 FIFO，它与 LRU 的思想更加接近，但是实现起来比 LRU 简单，并且开销更小。

## 页表映射方式

当操作系统启动时，`boot_page_table_sv39` 就采用了一个大页的映射方式，这一方式的好处是能够简便地将操作系统从物理地址的模式切换到虚拟地址模式而不用进行多级映射关系的处理。但是如果在多个进程的情况下，使用一个大页进行映射意味着在发生缺页异常和需要页面置换时需要把整个大页的内容（在 Sv39 下即为 1GiB）全部交换到硬盘上，在换回时也需要将所有的内容一起写回。在物理内存大小不够、进程数量较多而必须要进行置换时，这会造成程序运行速度的降低。此外，采用一个大页进行映射的方式还需要注意在页表项中设置这一内存的权限，如果一个程序存在着不同的段，将整个程序的代码段、数据段全部存入可执行的一个大页中可能会造成一系列的安全问题。
