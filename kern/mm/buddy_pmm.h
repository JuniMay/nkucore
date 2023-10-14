#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

#define BUDDY_MAX_ORDER 11

typedef struct {
    free_area_t free_area[BUDDY_MAX_ORDER];
} buddy_zone_t;

extern buddy_zone_t buddy_zone;

extern const struct pmm_manager buddy_pmm_manager;

#endif /* !__KERN_MM_BUDDY_PMM_H__ */