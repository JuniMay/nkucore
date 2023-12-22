# 第八次实验报告

## 完成读文件操作的实现

`sfs_io_nolock()` 函数用于在 SFS 文件系统中进行无锁的读写操作，首先对输入参数进行检查，然后根据是否为写操作选择对应的缓冲区/块操作函数，然后根据读写的开始位置和结束位置计算出需要读写的块数量，分别处理不与首个块对齐的情况、与块对齐的情况和不与最后一个块对齐的情况。

```c
static int
sfs_io_nolock(struct sfs_fs *sfs, struct sfs_inode *sin, void *buf, off_t offset, size_t *alenp, bool write) {
    struct sfs_disk_inode *din = sin->din;
    assert(din->type != SFS_TYPE_DIR); // 目录不支持读写操作
    
    off_t endpos = offset + *alenp, blkoff; // 计算读/写结束位置
    *alenp = 0;

    // 检查输入参数的有效性
    if (offset < 0 || offset >= SFS_MAX_FILE_SIZE || offset > endpos) {
        return -E_INVAL;
    }
    if (offset == endpos) {
        return 0;
    }
    if (endpos > SFS_MAX_FILE_SIZE) {
        endpos = SFS_MAX_FILE_SIZE;
    }

    // 根据是否为写操作选择对应的缓冲区操作函数
    int (*sfs_buf_op)(struct sfs_fs *sfs, void *buf, size_t len, uint32_t blkno, off_t offset);
    int (*sfs_block_op)(struct sfs_fs *sfs, void *buf, uint32_t blkno, uint32_t nblks);
    if (write) {
        sfs_buf_op = sfs_wbuf, sfs_block_op = sfs_wblock;
    } else {
        sfs_buf_op = sfs_rbuf, sfs_block_op = sfs_rblock;
    }

    int ret = 0;
    size_t size, alen = 0;
    uint32_t ino;
    uint32_t blkno = offset / SFS_BLKSIZE;          // 读/写开始块号
    uint32_t nblks = endpos / SFS_BLKSIZE - blkno;  // 读/写的块数量

```
(1) 处理不与首个块对齐的情况
```c
    if ((blkoff = offset % SFS_BLKSIZE) != 0) {
        size = (nblks != 0) ? (SFS_BLKSIZE - blkoff) : (endpos - offset); // 计算需要处理的字节数

        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        if ((ret = sfs_buf_op(sfs, buf, size, ino, blkoff)) != 0) {
            goto out;
        }
        alen += size;
        
        // 如果只有一个不块需要处理，那已经处理完毕，直接返回
        if (nblks == 0) {
            goto out;
        }
        blkno++;
        buf += size;
        nblks--;
    }
```
(2) 处理与块对齐的部分
```c
    size = SFS_BLKSIZE;
    while (nblks != 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }

        if ((ret = sfs_block_op(sfs, buf, ino, 1)) != 0) {
            goto out;
        }
        alen += size;
        
        buf += size;
        blkno++;
        nblks--;
    }
```
(3) 处理不与最后一个块对齐的情况
```c
    if ((size = endpos % SFS_BLKSIZE) != 0) {
        if ((ret = sfs_bmap_load_nolock(sfs, sin, blkno, &ino)) != 0) {
            goto out;
        }
        
        if ((ret = sfs_buf_op(sfs, buf, size, ino, 0)) != 0) {
            goto out;
        }
        alen += size;
    }
```
(4) 更新实际读/写的字节数并更新 inode 的大小，设置 dirty 标志
```c
out:
    *alenp = alen;
    if (offset + alen > sin->din->size) {
        sin->din->size = offset + alen;
        sin->dirty = 1;
    }

    return ret;
}
```

## 完成基于文件系统的执行程序机制的实现

增加了文件系统后，`proc.c` 需要进行一些修改。

1. 在 `alloc_proc()` 中，增加对文件系统 `struct files_struct * filesp` 的初始化。

    由于使用了 `memset(proc, 0, sizeof(struct proc_struct))`，会自动置 0， 因此不用添加其他代码。
2. 在 `do_fork()` 中，用 `copy_files()` 复制父进程的文件系统信息到子进程中。
    ```c
    if (copy_files(clone_flags, proc) != 0) { //for LAB8
        goto bad_fork_cleanup_kstack;
    }
    ```
3. 在 `load_icode()` 中，使用文件系统加载程序。
    用 `load_icode_read()` 替换 `load_icode_read()`
    ```c
    //(1) create a new mm for current process
    if ((mm = mm_create()) == NULL) {
        goto bad_mm;
    }
    //(2) create a new PDT, and mm->pgdir= kernel virtual addr of PDT
    if (setup_pgdir(mm) != 0) {
        goto bad_pgdir_cleanup_mm;
    }
    //(3) copy TEXT/DATA section, build BSS parts in binary to memory space of process
    struct Page *page;
    //(3.1) get the file header of the bianry program (ELF format)
    struct elfhdr elf_content;
    struct elfhdr *elf = &elf_content;
    //(3.2) get the entry of the program section headers of the bianry program (ELF format)
    struct proghdr ph_content;
    struct proghdr *ph = &ph_content;

    if ((ret = load_icode_read(fd, elf, sizeof(struct elfhdr), 0)) != 0) {
        goto bad_elf_cleanup_pgdir;
    }

    //(3.3) This program is valid?
    if (elf->e_magic != ELF_MAGIC) {
        ret = -E_INVAL_ELF;
        goto bad_elf_cleanup_pgdir;
    }
    
    uint32_t vm_flags, perm, phnum = 0;
    // struct proghdr *ph_end = ph + elf->e_phnum;
    for (; phnum < elf->e_phnum; phnum ++) {
        if ((ret = load_icode_read(fd, ph, sizeof(struct proghdr), elf->e_phoff + sizeof(struct proghdr) * phnum)) != 0) {
            goto bad_cleanup_mmap;
        }
        //(3.4) find every program section headers
        if (ph->p_type != ELF_PT_LOAD) {
            continue ;
        }
        if (ph->p_filesz > ph->p_memsz) {
            ret = -E_INVAL_ELF;
            goto bad_cleanup_mmap;
        }
        if (ph->p_filesz == 0) {
            // continue ;
        }
        //(3.5) call mm_map fun to setup the new vma ( ph->p_va, ph->p_memsz)
        vm_flags = 0, perm = PTE_U | PTE_V;
        if (ph->p_flags & ELF_PF_X) vm_flags |= VM_EXEC;
        if (ph->p_flags & ELF_PF_W) vm_flags |= VM_WRITE;
        if (ph->p_flags & ELF_PF_R) vm_flags |= VM_READ;
        // modify the perm bits here for RISC-V
        if (vm_flags & VM_READ) perm |= PTE_R;
        if (vm_flags & VM_WRITE) perm |= (PTE_W | PTE_R);
        if (vm_flags & VM_EXEC) perm |= PTE_X;
        if ((ret = mm_map(mm, ph->p_va, ph->p_memsz, vm_flags, NULL)) != 0) {
            goto bad_cleanup_mmap;
        }
        off_t offset = ph->p_offset;
        size_t off, size;
        uintptr_t start = ph->p_va, end, la = ROUNDDOWN(start, PGSIZE);

        ret = -E_NO_MEM;

        end = ph->p_va + ph->p_filesz;
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                ret = -E_NO_MEM;
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            if ((ret = load_icode_read(fd, page2kva(page) + off, size, offset)) != 0) {
                goto bad_cleanup_mmap;
            }
            start += size, offset += size;
        }

        //(3.6.2) build BSS section of binary program
        end = ph->p_va + ph->p_memsz;
        if (start < la) {
            /* ph->p_memsz == ph->p_filesz */
            if (start == end) {
                continue ;
            }
            off = start + PGSIZE - la, size = PGSIZE - off;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
            assert((end < la && start == end) || (end >= la && start == la));
        }
        while (start < end) {
            if ((page = pgdir_alloc_page(mm->pgdir, la, perm)) == NULL) {
                ret = -E_NO_MEM;
                goto bad_cleanup_mmap;
            }
            off = start - la, size = PGSIZE - off, la += PGSIZE;
            if (end < la) {
                size -= la - end;
            }
            memset(page2kva(page) + off, 0, size);
            start += size;
        }
    }

    sysfile_close(fd);
    ```

## 扩展练习 Challenge1：完成基于“UNIX的PIPE机制”的设计方案

定义管道的缓冲区
```c
struct pipe_buffer {
    struct page *page; // 缓冲区所在的页
    unsigned int offset, len; // 缓冲区的偏移量和长度
    unsigned int flags; 
};
```

定义管道的结构体
```c
struct pipe_inode_info {
    wait_queue_t wait; // 等待队列
    struct pipe_buffer *bufs; // 缓冲区数组
    unsigned int nrbufs, curbuf; // 缓冲区数量和当前缓冲区
    unsigned int readers, writers; // 读者和写者数量
    unsigned int waiting_writers; // 等待写者数量
    struct inode *inode; // 管道对应的 inode
    list_entry_t pipe_inode_link; // 管道链表
};
```

创建管道
```c
int pipe(int pipefd[2]);
```

管道的读操作
```c
int read(int fd, void *buf, size_t count);
```

管道的写操作
```c
int write(int fd, const void *buf, size_t count);
```

