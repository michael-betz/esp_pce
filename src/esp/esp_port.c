#include <drivers/video/terminal.h>
#include <drivers/video/null.h>
#include <drivers/block/block.h>

#include <devices/memory.h>

#include <lib/log.h>
#include <lib/console.h>
#include <lib/monitor.h>
#include <lib/cmd.h>
#include <lib/sysdep.h>

#include "main.h"
#include "cmd_68k.h"
#include "macplus.h"
#include "msg.h"
#include "sony.h"

#include "esp_partition.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "oled.h"
#include "mipi_dsi.h"

monitor_t  par_mon;
const char *par_terminal = NULL;
macplus_t  *par_sim = NULL;


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


static void esp_trm_update (terminal_t *trm)
{
	printf(
		"esp_trm_update(x %d, y %d, w %d, h %d)\n",
		trm->update_x,
		trm->update_y,
		trm->update_w,
		trm->update_h
	);

    setColRange(trm->update_x, trm->update_x + trm->update_w - 1);
    setRowRange(trm->update_y, trm->update_y + trm->update_h - 1);
    mipiDsiSendLong(0x39, 0x2c, trm->buf, trm->buf_cnt);
}


terminal_t *ini_get_terminal (const char *def)
{
	terminal_t *trm = NULL;

	// if ((trm = malloc(sizeof(terminal_t))) == NULL) {
	// 	return NULL;
	// }

	trm = null_new(NULL);

	// trm_init (trm, trm);

	// trm->del = (void *) trm_del;
	// trm->open = (void *) trm_open;
	// trm->close = (void *) trm_close;
	// trm->set_msg_trm = (void *) trm_set_msg_trm;
	trm->update = (void *) esp_trm_update;
	// sdl->trm.check = (void *) trm_check;

	return trm;
}


void emu_task(void *pvParameters)
{
    // Initialize the DSI interface
    mipiInit();
    initOled();

	pce_log_init();
	pce_log_add_fp (stderr, 0, MSG_DEB);
	mac_log_banner();

	pce_console_init (stdin, stdout);

	par_sim = mac_new (NULL);

	mon_init (&par_mon);
	mon_set_cmd_fct (&par_mon, mac_cmd, par_sim);
	mon_set_msg_fct (&par_mon, mac_set_msg, par_sim);
	mon_set_get_mem_fct (&par_mon, par_sim->mem, mem_get_uint8);
	mon_set_set_mem_fct (&par_mon, par_sim->mem, mem_set_uint8);
	mon_set_set_memrw_fct (&par_mon, par_sim->mem, mem_set_uint8_rw);
	mon_set_memory_mode (&par_mon, 0);

	cmd_init (par_sim, cmd_get_sym_mac, cmd_set_sym_mac);
	mac_cmd_init (par_sim, &par_mon);

	mac_reset (par_sim);

	mac_run (par_sim);

	// pce_puts ("type 'h' for help\n");
	// if (par_sim->brk != PCE_BRK_ABORT) {
	// 	mon_run (&par_mon);
	// }

	mac_del (par_sim);
	mon_free (&par_mon);
	pce_console_done();
	pce_log_done();

    vTaskDelete(NULL);
}


#define LINE_LEN (311)

// Draw grey bars, which should not be at an angle if LINE_LEN
// matches the row-range of the draw window
// static void disp()
// {
//     static unsigned iter = 0;
//     static uint8_t clrData[LINE_LEN * 2];
//     uint8_t cmd;

//     uint8_t *p = clrData;
//     for (unsigned i = 0; i < LINE_LEN; i++) {
//         // native color format is RGB 565
//         // here we drive it in RGB 555 to get white
//         unsigned r = (i) & 0x1F;
//         unsigned g = (i) & 0x1F;
//         unsigned b = (i) & 0x1F;
//         unsigned tmp = (b << 11) | (g << 6) | r;
//         *p++ = tmp;
//         *p++ = tmp >> 8;
//     }

//     if (iter == 0)
//         cmd = 0x2C;  // CMD: write from start
//     else
//         cmd = 0x3C;  // CMD: continue write

//     // printf("row %d\n", iter);

//     mipiDsiSendLong(0x39, cmd, clrData, sizeof(clrData));

//     iter++;
// }

// void oled_task(void *pvParameters)
// {
//     // Initialize the DSI interface
//     mipiInit();
//     initOled();

//     setRowRange(5, 319 - 5);
//     setColRange(5, 319 - 5);
//     while (1) {
//         disp();
//         // vTaskDelay(1 / portTICK_PERIOD_MS);
//     }
// }

void app_main(void)
{
	printf("Hello, this is esp_pce!\n");

	// printf("Starting oled_task...\n");
	// xTaskCreatePinnedToCore(&oled_task, "oled", 12 * 1024, NULL, 5, NULL, 1);

	printf("Starting emu_task...\n");
	xTaskCreatePinnedToCore(&emu_task, "emu", 20 * 1024, NULL, 5, NULL, 0);
}
