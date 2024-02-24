#pragma once
#include <drivers/block/block.h>
#include <devices/memory.h>

// used to memory-map the rom data
int map_partition (mem_blk_t *blk, unsigned long size, const char* part_name);

// hdd flash access functions
disk_t *flash_disk_init(const char *part_name, unsigned drive_id);
// void dsk_part_del (disk_t *dsk);
// int dsk_part_read (disk_t *dsk, void *buf, uint32_t i, uint32_t n);
// int dsk_part_write (disk_t *dsk, const void *buf, uint32_t i, uint32_t n);
