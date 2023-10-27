# 第三次实验报告

缺页异常与页面置换。

## FIFO 页面替换算法

## 不同分页模式的工作原理

### 两段相似的代码

RISC-V 中目前共有四种分页模式 Sv32，Sv39，Sv48，Sv57。其中 Sv32 使用了二级页表，Sv39 使用了三级页表，Sv48 和 Sv57 分别使用了四级和五级页表。

`get_pte` 的目的是查找/创建特定线性地址对应的页表项。由于 ucore 使用 Sv39 的分页机制，所以共使用了三级页表，`get_pte` 中的两段相似的代码分别对应于第三、第二级页表（页表目录）的查找/创建过程。首先 `get_pte` 函数接收一个参数 `create` 标识是否要分配新页。之后首先通过 `PDX1` 宏获取线性地址 `la` 对应的第一级 `VPN2` 的值，并且在 `pgdir` 即根页表（页目录）中找到对应的地址，并且判断其 `V` 标志位是否为真（表示可用）。若可用则继续寻找下一级目录，若不可用则根据 `create` 或 `alloc_page()` 的结果设置这一级页表的页表项，包括页面的引用以及下一级页表的页号的对应关系。之后再对下一级页表进行同样的操作，其中通过 `PDE_ADDR` 得到页表项对应的物理地址。最后根据得到的 `pdep0` 的页表项找到最低一级页表项的内容并且返回。

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

### 缺页服务例程中的页访问异常

### Page 与页目录项、页表项的对应关系

## Clock 页面替换算法

## 页表映射方式

