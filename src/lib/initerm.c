/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/lib/initerm.c                                            *
 * Created:     2008-10-21 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2008-2012 Hampa Hug <hampa@hampa.ch>                     *
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


#include <config.h>

#include <string.h>

#include <lib/initerm.h>
#include <lib/log.h>
#include <lib/libini.h>

#include <drivers/video/terminal.h>
#include <drivers/video/null.h>

#include <pce-mac-plus.cfg.h>
#include <../sdl_sim/sdl2.h>


terminal_t *ini_get_terminal (ini_sct_t *ini, const char *def)
{
	ini_sct_t  *sct = NULL;
	terminal_t *trm = NULL;

	trm = sdl2_new ();

	if (trm == NULL) {
		pce_log (MSG_ERR, "*** setting up sdl2 terminal failed\n");
	}

	// trm = null_new (sct);

	// if (trm == NULL) {
	// 	pce_log (MSG_ERR, "*** setting up null terminal failed\n");
	// }

	trm_set_escape_str (trm, TERMINAL_ESCAPE);
	trm_set_scale (trm, TERMINAL_SCALE);
	trm_set_min_size (trm, 512, 384);
	trm_set_aspect_ratio (trm, TERMINAL_ASPECT_X, TERMINAL_ASPECT_Y);
	trm_set_mouse_scale (trm, TERMINAL_MOUSE_MUL_X, TERMINAL_MOUSE_DIV_X, TERMINAL_MOUSE_MUL_Y, TERMINAL_MOUSE_DIV_Y);

	return (trm);
}
