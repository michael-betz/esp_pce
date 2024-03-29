/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/macplus/main.c                                      *
 * Created:     2007-04-15 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2007-2022 Hampa Hug <hampa@hampa.ch>                     *
 *****************************************************************************/

/*****************************************************************************
 * This program is free software. You can redistribute it and / or modify it *
 * under the terms of the GNU General Public License version 2 as  published *
 * by the Free Software Foundation.                                          *
 *                                                                           *
 * This program is distributed in the hope  that  it  will  be  useful,  but *
 * WITHOUT  ANY   WARRANTY,   without   even   the   implied   warranty   of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General *
 * Public License for more details.                                          *
 *****************************************************************************/


#include "main.h"
#include "cmd_68k.h"
#include "macplus.h"
#include "msg.h"
#include "sony.h"
#include "sdl2.h"

#include <stdarg.h>
#include <time.h>

#include <unistd.h>
#include <signal.h>

#include <lib/cmd.h>
#include <lib/console.h>
#include <lib/getopt.h>
#include <lib/log.h>
#include <lib/monitor.h>
#include <lib/path.h>
#include <lib/sysdep.h>

#include <drivers/video/terminal.h>
#include <drivers/video/null.h>

#include <lib/libini.h>
#include <SDL2/SDL.h>


const char *par_terminal = NULL;

unsigned   par_disk_boot = 0;

macplus_t  *par_sim = NULL;

unsigned   par_sig_int = 0;

monitor_t  par_mon;

ini_sct_t  *par_cfg = NULL;


static pce_option_t opts[] = {
	{ '?', 0, "help", NULL, "Print usage information" },
	{ 'b', 1, "boot-disk", "int", "Set the boot disk [none]" },
	{ 'c', 1, "config", "string", "Set the config file name [none]" },
	{ 'd', 1, "path", "string", "Add a directory to the search path" },
	{ 'i', 1, "ini-prefix", "string", "Add an ini string before the config file" },
	{ 'I', 1, "ini-append", "string", "Add an ini string after the config file" },
	{ 'l', 1, "log", "string", "Set the log file name [none]" },
	{ 'p', 1, "cpu", "string", "Set the CPU model" },
	{ 'q', 0, "quiet", NULL, "Set the log level to error [no]" },
	{ 'r', 0, "run", NULL, "Start running immediately [no]" },
	{ 'R', 0, "no-monitor", NULL, "Never stop running [no]" },
	{ 's', 1, "speed", "int", "Set the CPU speed" },
	{ 't', 1, "terminal", "string", "Set the terminal device" },
	{ 'v', 0, "verbose", NULL, "Set the log level to debug [no]" },
	{ 'V', 0, "version", NULL, "Print version information" },
	{  -1, 0, NULL, NULL, NULL }
};


static
void print_help (void)
{
	pce_getopt_help (
		"pce-macplus: Macintosh Plus emulator",
		"usage: pce-macplus [options]",
		opts
	);

	fflush (stdout);
}

void sig_int (int s)
{
	par_sig_int = 1;
}

void sig_segv (int s)
{
	pce_set_fd_interactive (0, 1);

	fprintf (stderr, "pce-macplus: segmentation fault\n");
	fflush (stderr);

	if ((par_sim != NULL) && (par_sim->cpu != NULL)) {
		mac_prt_state (par_sim, "cpu");
	}

	exit (1);
}

void sig_term (int s)
{
	pce_set_fd_interactive (0, 1);

	fprintf (stderr, "pce-macplus: signal %d\n", s);
	fflush (stderr);

	exit (1);
}

static
void mac_atexit (void)
{
	pce_set_fd_interactive (0, 1);
}

void sim_stop (void)
{
	macplus_t *sim = par_sim;

	pce_prt_sep ("BREAK");
	mac_prt_state (sim, "cpu");

	mac_set_msg (sim, "emu.stop", NULL);
}

void mac_stop (macplus_t *sim)
{
	if (sim == NULL) {
		sim = par_sim;
	}

	mac_set_msg (sim, "emu.stop", NULL);
}

// void app_main ()
// {
// 	pce_log_init();
// 	pce_log_add_fp (stderr, 0, MSG_DEB);

// 	mac_log_banner();

// 	atexit (mac_atexit);

// 	pce_console_init (stdin, stdout);

// 	par_sim = mac_new (NULL);

// 	mon_init (&par_mon);
// 	mon_set_cmd_fct (&par_mon, mac_cmd, par_sim);
// 	mon_set_msg_fct (&par_mon, mac_set_msg, par_sim);
// 	mon_set_get_mem_fct (&par_mon, par_sim->mem, mem_get_uint8);
// 	mon_set_set_mem_fct (&par_mon, par_sim->mem, mem_set_uint8);
// 	mon_set_set_memrw_fct (&par_mon, par_sim->mem, mem_set_uint8_rw);
// 	mon_set_memory_mode (&par_mon, 0);

// 	cmd_init (par_sim, cmd_get_sym, cmd_set_sym);
// 	mac_cmd_init (par_sim, &par_mon);

// 	mac_reset (par_sim);

// }

int main (int argc, char *argv[])
{
	int       r;
	char      **optarg;
	int       run, nomon;
	unsigned  drive;

	run = 0;
	nomon = 0;

	pce_log_init();
	pce_log_add_fp (stderr, 0, MSG_DEB);

	while (1) {
		r = pce_getopt (argc, argv, &optarg, opts);

		if (r == GETOPT_DONE) {
			break;
		}

		if (r < 0) {
			return (1);
		}

		switch (r) {
		case '?':
			print_help();
			return (0);

		case 'V':
			print_version();
			return (0);

		case 'b':
			drive = (unsigned) strtoul (optarg[0], NULL, 0);

			if ((drive >= 1) && (drive <= 8)) {
				par_disk_boot |= 1U << (drive - 1);
			}
			break;

		case 'd':
			pce_path_set (optarg[0]);
			break;

		case 'l':
			pce_log_add_fname (optarg[0], MSG_DEB);
			break;

		case 'q':
			pce_log_set_level (stderr, MSG_ERR);
			break;

		case 'r':
			run = 1;
			break;

		case 'R':
			nomon = 1;
			break;

		case 't':
			par_terminal = optarg[0];
			break;

		case 'v':
			pce_log_set_level (stderr, MSG_DEB);
			break;

		case 0:
			fprintf (stderr, "%s: unknown option (%s)\n",
				argv[0], optarg[0]
			);
			return (1);

		default:
			return (1);
		}
	}

	mac_log_banner();

	atexit (mac_atexit);

	SDL_Init (0);

	signal (SIGINT, &sig_int);
	signal (SIGSEGV, &sig_segv);
	signal (SIGTERM, &sig_term);

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

	if (nomon) {
		while (par_sim->brk != PCE_BRK_ABORT) {
			mac_run (par_sim);
		}
	}
	else if (run) {
		mac_run (par_sim);
		if (par_sim->brk != PCE_BRK_ABORT) {
			pce_puts ("\n");
		}
	}
	else {
		pce_puts ("type 'h' for help\n");
	}

	if (par_sim->brk != PCE_BRK_ABORT) {
		mon_run (&par_mon);
	}

	mac_del (par_sim);

#ifdef PCE_ENABLE_SDL
	SDL_Quit();
#endif

	mon_free (&par_mon);
	pce_console_done();
	pce_log_done();

	return (0);
}

terminal_t *ini_get_terminal (const char *def)
{
	terminal_t *trm = NULL;
	sdl2_t *sdl;

	if ((sdl = malloc (sizeof (sdl2_t))) == NULL) {
		return (NULL);
	}

	sdl2_init (sdl);

	trm = &sdl->trm;

	if (trm == NULL) {
		pce_log (MSG_ERR, "*** setting up sdl2 terminal failed\n");
	}

	return (trm);
}
