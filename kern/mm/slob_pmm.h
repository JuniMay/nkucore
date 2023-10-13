#ifndef __KERN_MM_SLOB_PMM_H__
#define  __KERN_MM_SLOB_PMM_H__

#include <pmm.h>

#define ALIGN_SIZE 8
#define SLOB_UNIT 2
#define SLOB_SMALL 256
#define SLOB_MEDIUM 1024

#define le2

typedef struct {
    uint16_t obj_size;
    free_area_t free_slob_small;
    free_area_t free_slob_medium;
    free_area_t free_slob_large;
} slob_manager_t;

typedef struct {
    uint16_t slob_units_left;
    uint16_t slob_next_offset;
} slob_t;

extern slob_manager_t slob_manager;
extern const struct pmm_manager slob_pmm_manager;
extern char end[];

#endif /* ! __KERN_MM_SLOB_PMM_H__ */
