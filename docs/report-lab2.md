# 第二次实验报告

物理内存和页表

`pmm_init` 之后的内存布局

```text

+-----------------+ <-- vma: 0xffffffffc0200000
|                 |     pma:         0x80200000
|      KERNEL     |
|                 |
+-----------------+ <-- pma:         0x80206470
|       HOLE      | ---------------> ROUNDUP
+-----------------+ <-- pma:         0x80207000
|                 |
|     `Page`s     | ---------------> Array of `Page` structs
|                 |
+-----------------+ <-- mem_begin (ROUNDUP, but the same)
|                 |     pma:         0x80347000
|       Mem       |
|                 |
+-----------------+ <-- mem_end (ROUNDDOWN, but the same)
                        pma:         0x88000000
```

## Firsr-fit 连续物理内存分配算法

在 `pmm_init` 之后，会使用 `pa2page` 将 `mem_begin` 对应的物理地址转换为对应的 `Page`。并与 `mem_begin` 到 `mem_end` 这一段内存中包含的页的数量一起传入 `init_memmap`，之后再调用 `pmm_manager` 的 `init_memmap`。

## Best-fit 连续物理内存分配算法

## Buddy System

## Slub

## 硬件可用物理内存范围的获取方法
