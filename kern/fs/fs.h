#ifndef __KERN_FS_FS_H__
#define __KERN_FS_FS_H__

#include <mmu.h>

#define SECTSIZE            512 // sector size
#define PAGE_NSECT          (PGSIZE / SECTSIZE) // number of sectors per page

#define SWAP_DEV_NO         1

#endif /* !__KERN_FS_FS_H__ */

