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
用户接口：

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

内核会创建两个 file 结构体，分别对应管道的读端和写端，指向同一个 inode，但是 file 结构体的 `f_pos` 不同，分别指向管道的读写位置，返回给用户两个文件描述符，分别对应管道的读端和写端。用户需要进行读写操作时，通过文件描述符找到对应的 file 结构体，然后通过 file 结构体找到 inode，再通过 inode 找到管道的 inode 信息，然后进行读写操作。

## 扩展练习 Challenge2：完成 基于 UNIX 的软连接和硬连接机制 的设计方案



由于硬链接是有着相同 inode 号仅文件名不同的文件，因此硬链接存在以下几点特性：

- 硬链接是目录中的一个额外的目录项，指向相同的索引节点（inode）。
- 硬链接与目标文件没有物理上的区别，它们共享相同的数据块。
- 不允许为目录创建硬链接。
- 硬链接不能跨越文件系统。
- 修改任何一个硬链接都会影响其他硬链接，因为它们指向相同的数据块。
- 删除任何一个硬链接并不会影响其他硬链接，只有当所有硬链接都被删除时，文件的数据块才会被释放。

软链接特性：

- 软链接是一个包含目标文件路径的特殊文件。
- 它仅包含目标文件的路径信息，而不是实际的文件数据。
- 可以链接到文件、目录或者其他软链接。
- 软链接可以跨越文件系统。
- 修改软链接不会影响目标文件，但删除目标文件会导致软链接失效。
- 软链接可以链接到不存在的目标。

在文件系统中，每个文件或目录都与一个唯一的 inode 相关联，而硬链接和软链接是通过对 inode 的不同操作来实现的。

- **硬链接的创建：**
  - 创建硬链接时，系统为新路径（new_path）创建一个文件，并将其 inode 指向原路径（old_path）所对应的 inode。
  - 同时，原路径所对应 inode 的引用计数增加，表示有一个额外的硬链接指向它。

- **软链接的创建：**
  - 创建软链接时，系统创建一个新的文件（新的 inode），并将其内容设置为原路径的内容。
  - 在磁盘上保存该文件时，该文件的 inode 类型被标记为 `SFS_TYPE_LINK`，同时需要对这种类型的 inode 进行相应的操作。

- **删除硬链接：**
  - 删除硬链接时，除了需要删除硬链接的 inode，还需要将该硬链接所指向的文件的被链接计数减1。
  - 如果减到了0，表示没有其他硬链接指向该文件，此时需要将该文件对应的 inode 从磁盘上删除。

- **删除软链接：**
  - 删除软链接时，只需将软链接对应的 inode 从磁盘上删除即可。


- **访问链接的方式：**
  - 无论是硬链接还是软链接，访问链接的方式是一致的，通过链接路径即可访问链接指向的文件。

总的来说，硬链接通过共享相同的 inode 实现，而软链接则是创建一个新的 inode，并在其内容中保存原路径的信息。硬链接的删除需要注意被链接文件的引用计数，而软链接的删除则相对简单。无论是硬链接还是软链接，它们提供了一种有效的方式来在文件系统中建立链接和引用关系。

数据结构：

在设计硬链接和软链接机制时，需要对 inode 进行扩展，并引入新的数据结构表示链接。

```c
// 软链接结构
struct link {
    struct inode *inode;  // 指向Inode的指针
    int link_count;       // 链接计数
    bool is_symlink;      // 是否是软链接
};


// 文件信息结构（inode）扩展
struct inode {
    // 其他字段
    int ref_count;        // 引用计数
    // ...
};

```

#### ***函数接口***

```c
// 创建硬链接
int create_hlink(const char *source_path, const char *target_path);

// 创建软链接
int create_slink(const char *source_path, const char *target_path);

// 删除链接
int remove_link(const char *link_path);
```


