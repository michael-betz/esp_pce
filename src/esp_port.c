#include <drivers/video/terminal.h>
#include <drivers/video/null.h>
#include <devices/memory.h>
#include <drivers/block/block.h>
#include <lib/log.h>

#include "main.h"
#include "macplus.h"

#include "esp_partition.h"

// Maps a flash partition to a PCE memory-block
int map_partition (mem_blk_t *blk, unsigned long size, const char* part_name)
{
	const esp_partition_t *part = NULL;

	if (blk == NULL) {
		return 1;
	}

	// map partition from flash into memory
	part = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, part_name);
	if (part == NULL) {
		pce_log (MSG_ERR, "*** couldn't find partition %s\n", part_name);
		return 1;
	}
	esp_partition_mmap_handle_t hrom;
	unsigned char *mappeddata;
	esp_err_t err = esp_partition_mmap(part, 0, size, ESP_PARTITION_MMAP_DATA, (const void**)&mappeddata, &hrom);
	if (err != ESP_OK) {
		pce_log (MSG_ERR, "*** couldn't memory-map partition %s\n", part_name);
		return 1;
	}
	blk->data = mappeddata;

	return 0;
}


static void dsk_part_del (disk_t *dsk)
{
}

static int dsk_part_read (disk_t *dsk, void *buf, uint32_t i, uint32_t n)
{
	esp_partition_t *part = (esp_partition_t *)dsk->ext;

	if ((i + n) > dsk->blocks)
		return 1;

	if (esp_partition_read(part, 512 * i, buf, 512 * n) != ESP_OK)
		return 1;

	return 0;
}

static int dsk_part_write (disk_t *dsk, const void *buf, uint32_t i, uint32_t n)
{
	esp_partition_t *part = (esp_partition_t *)dsk->ext;
	const uint8_t *data = buf;
	// unsigned erase_size = part->erase_size;
	const unsigned erase_size = 4096;

	if (dsk->readonly) {
		return (1);
	}

	if ((i + n) > dsk->blocks) {
		return (1);
	}

	// uint8_t *secdat = malloc(erase_size);
	uint8_t secdat[erase_size];
	// if (secdat == NULL)
	// 	return 1;

	unsigned int lbaStart = i & (~7);
	unsigned int lbaOff = i & 7;

	if (esp_partition_read(part, lbaStart * 512, secdat, erase_size) != ESP_OK)
		return 1;

	if (esp_partition_erase_range(part, lbaStart * 512, erase_size) != ESP_OK)
		return 1;

	for (int i = 0; i < 512; i++)
		secdat[lbaOff * 512 + i] = data[i];

	if (esp_partition_write(part, lbaStart * 512, secdat, erase_size) != ESP_OK)
		return 1;

	// free(secdat);

	return (0);
}

disk_t *flash_disk_init(const char *part_name, unsigned drive_id)
{
	const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, part_name);
	if (part == 0) {
		pce_log (MSG_ERR, "*** couldn't find partition %s\n", part_name);
		return NULL;
	}

	disk_t *dsk = malloc(sizeof(disk_t));
	if (dsk == NULL) {
		return (NULL);
	}

	dsk_init (dsk, (void *)part, part->size / 512, 0, 0, 0);
	dsk_set_type (dsk, PCE_DISK_RAW);
	dsk_set_readonly (dsk, 0);
	dsk_set_fname (dsk, part->label);
	dsk->del = dsk_part_del;
	dsk->read = dsk_part_read;
	dsk->write = dsk_part_write;

	dsk_set_drive (dsk, drive_id);
	return dsk;
}


terminal_t *ini_get_terminal (const char *def)
{
	terminal_t *trm = NULL;

	trm = null_new (NULL);

	if (trm == NULL) {
		pce_log (MSG_ERR, "*** setting up null terminal failed\n");
	}

	return (trm);
}


void app_main(void)
{
	printf("Hello, this is esp_pce!\n");

	pce_log_init();
	pce_log_add_fp (stderr, 0, MSG_DEB);
	mac_log_banner();

}
