#include <assert.h>
#include <defs.h>
#include <fs.h>
#include <ide.h>
#include <stdio.h>
#include <string.h>
#include <trap.h>
#include <riscv.h>

void ide_init(void) {}

#define MAX_IDE 2
#define MAX_DISK_NSECS 56
static char ide[MAX_DISK_NSECS * SECTSIZE]; // use static memory to simulate disk

// check if ideno is valid (ideno < MAX_IDE)
bool ide_device_valid(unsigned short ideno) { 
    return ideno < MAX_IDE; 
}

// return max disk size (in sectors)
size_t ide_device_size(unsigned short ideno) {
    return MAX_DISK_NSECS; 
}

// simulate read disk using memcpy
// ideno: ide number
// secno: sector number 
// dst: destination address
// nsecs: number of sectors to read
int ide_read_secs(unsigned short ideno, uint32_t secno, void *dst,
                  size_t nsecs) {
    int iobase = secno * SECTSIZE;
    memcpy(dst, &ide[iobase], nsecs * SECTSIZE);
    return 0;
}

// simulate write disk using memcpy
// ideno: ide number
// secno: sector number
// src: source address
// nsecs: number of sectors to write
int ide_write_secs(unsigned short ideno, uint32_t secno, const void *src,
                   size_t nsecs) {
    int iobase = secno * SECTSIZE;
    memcpy(&ide[iobase], src, nsecs * SECTSIZE);
    return 0;
}
