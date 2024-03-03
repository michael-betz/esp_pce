/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/lib/iniram.c                                             *
 * Created:     2005-07-24 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2005-2010 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU  General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include <stdlib.h>

#include <lib/iniram.h>
#include <lib/load.h>
#include <lib/log.h>
#include <lib/path.h>
#include <lib/libini.h>

#include <pce-mac-plus.cfg.h>
#include "esp/esp_port.h"

#ifndef SDL_SIM
	#define SDL_SIM 0
#endif


int ini_get_ram (memory_t *mem, mem_blk_t **addr0)
{
	mem_blk_t     *ram;

	if (addr0 != NULL) {
		*addr0 = NULL;
	}

	pce_log_tag (MSG_INF, "RAM:", "addr=0x%08lx size=%lu\n",
		RAM_ADDRESS, RAM_SIZE
	);

	ram = mem_blk_new (RAM_ADDRESS, RAM_SIZE, 1);
	if (ram == NULL) {
		pce_log (MSG_ERR, "*** memory block creation failed\n");
		return (1);
	}

	mem_blk_clear (ram, RAM_DEFAULT);
	mem_blk_set_readonly (ram, 0);
	mem_add_blk (mem, ram, 1);

	if ((addr0 != NULL) && (RAM_ADDRESS == 0)) {
		*addr0 = ram;
	}

	return (0);
}

int ini_get_rom (memory_t *mem)
{
	mem_blk_t     *rom;


	rom = mem_blk_new (ROM_ADDRESS, ROM_SIZE, SDL_SIM);
	if (rom == NULL) {
		pce_log (MSG_ERR, "*** memory block creation failed\n");
		return (1);
	}

	#ifdef SDL_SIM
		pce_log_tag (MSG_INF, "ROM:", "addr=0x%08lx size=%lu file=%s\n",
			ROM_ADDRESS, ROM_SIZE, ROM_FILE_NAME
		);
		mem_blk_clear (rom, 0);
		if (pce_load_blk_bin (rom, ROM_FILE_NAME)) {
			pce_log (MSG_ERR, "*** loading rom failed (%s)\n", ROM_FILE_NAME);
			return (1);
		}
	#else
		pce_log_tag (MSG_INF, "ROM:", "addr=0x%08lx size=%lu partition=%s\n",
			ROM_ADDRESS, ROM_SIZE, ROM_PARTITION_NAME
		);
		// map ESP partition from flash into memory
		if (map_partition(rom, ROM_SIZE, ROM_PARTITION_NAME))
			return (1);
	#endif

	mem_blk_set_readonly (rom, 1);
	mem_add_blk (mem, rom, 1);

	return (0);
}
