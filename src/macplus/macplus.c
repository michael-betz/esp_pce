/*****************************************************************************
 * pce                                                                       *
 *****************************************************************************/

/*****************************************************************************
 * File name:   src/arch/macplus/macplus.c                                   *
 * Created:     2007-04-15 by Hampa Hug <hampa@hampa.ch>                     *
 * Copyright:   (C) 2007-2023 Hampa Hug <hampa@hampa.ch>                     *
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
#include "hook.h"
#include "hotkey.h"
#include "iwm.h"
#include "keyboard.h"
#include "macplus.h"
#include "mem.h"
#include "msg.h"
#include "rtc.h"
#include "scsi.h"
#include "serial.h"
#include "sony.h"
#include "sound.h"
#include "video.h"

#include <string.h>

#include <chipset/e6522.h>
#include <chipset/e8530.h>

#include <cpu/e68000/e68000.h>

#include <devices/memory.h>
#include <devices/nvram.h>

#include <drivers/block/block.h>
#include <drivers/block/blkraw.h>
#include <drivers/video/terminal.h>

#include <lib/brkpt.h>
#include <lib/iniram.h>
#include <lib/load.h>
#include <lib/log.h>
#include <lib/sysdep.h>

#include <lib/libini.h>
#include <pce-mac-plus.cfg.h>
#include "esp/esp_port.h"


/* The CPU is synchronized with real time MAC_CPU_SYNC times per seconds */
#define MAC_CPU_SYNC 250

#ifdef PCE_HOST_WINDOWS
#define MAC_CPU_SLEEP 20000
#else
#define MAC_CPU_SLEEP 10000
#endif


static
unsigned char par_classic_pwm[64] = {
	0x00, 0x01, 0x3b, 0x02, 0x3c, 0x28, 0x36, 0x03,
	0x3d, 0x20, 0x31, 0x29, 0x37, 0x13, 0x23, 0x04,
	0x3e, 0x34, 0x1e, 0x21, 0x32, 0x0c, 0x0e, 0x2a,
	0x38, 0x10, 0x1b, 0x14, 0x24, 0x17, 0x2c, 0x05,
	0x3f, 0x3a, 0x27, 0x35, 0x1f, 0x30, 0x12, 0x22,
	0x33, 0x1d, 0x0b, 0x0d, 0x0f, 0x1a, 0x16, 0x2b,
	0x39, 0x26, 0x2f, 0x11, 0x1c, 0x0a, 0x19, 0x15,
	0x25, 0x2e, 0x09, 0x18, 0x2d, 0x08, 0x07, 0x06
};

static
void mac_classic_set_pwm (macplus_t *sim, const unsigned char *buf, unsigned cnt)
{
	unsigned i, v;

	v = 0;

	for (i = 0; i < cnt; i++) {
		v += par_classic_pwm[buf[0] & 0x3f];
	}

	v = v / cnt;

	if (v < 1) {
		v = 1;
	}
	else if (v > 31) {
		v = 31;
	}

	v = 32 + (223 * (30 - (v - 1)) + 15) / 30;

	if ((sim->video != NULL) && (sim->video->brightness != v)) {
		mac_video_set_brightness (sim->video, v);
	}
}

static
void mac_interrupt_check (macplus_t *sim)
{
	unsigned i;
	unsigned val;

	val = sim->intr >> 1;

	i = 0;
	while (val != 0) {
		i += 1;
		val = val >> 1;
	}

#ifdef DEBUG_INT
	mac_log_deb ("interrupt level %u\n", i);
#endif

	e68_interrupt (sim->cpu, i);
}

void mac_interrupt (macplus_t *sim, unsigned level, int val)
{
	if (val) {
		sim->intr |= (1U << level);
	}
	else {
		sim->intr &= ~(1U << level);
	}

	mac_interrupt_check (sim);
}

static
void mac_interrupt_via (void *ext, unsigned char val)
{
	macplus_t *sim = ext;

	if (val) {
		sim->intr_scsi_via |= 1;
	}
	else {
		sim->intr_scsi_via &= ~1;
	}

	mac_interrupt (ext, 1, sim->intr_scsi_via);
}

static
void mac_interrupt_scsi (void *ext, unsigned char val)
{
	macplus_t *sim = ext;

	if (sim->via_port_b & 0x40) {
		val = 0;
	}
	else {
		val = (val != 0);
	}

	if (val) {
		sim->intr_scsi_via |= 2;
	}
	else {
		sim->intr_scsi_via &= ~2;
	}

	mac_interrupt (sim, 1, sim->intr_scsi_via);
}

static
void mac_interrupt_scc (void *ext, unsigned char val)
{
	mac_interrupt (ext, 2, val);
}

static
void mac_interrupt_vbi (void *ext, unsigned char val)
{
	unsigned      i;
	unsigned char pbuf[370];
	macplus_t     *sim = ext;

	if (val) {
		e6522_set_ca1_inp (&sim->via, 0);
		e6522_set_ca1_inp (&sim->via, 1);

		mac_sound_vbl (&sim->sound);

		for (i = 0; i < 370; i++) {
			pbuf[i] = mem_get_uint8 (sim->mem, sim->sbuf1 + i + 1);
		}

		if (sim->model & PCE_MAC_CLASSIC) {
			mac_classic_set_pwm (sim, pbuf, 370);
		}
		else {
			mac_iwm_set_pwm (&sim->iwm, pbuf, 370);
		}
	}
}

static
void mac_interrupt_sony_check (macplus_t *sim)
{
	unsigned long a7;

#ifdef DEBUG_SONY
	mac_log_deb ("sony: check\n");
#endif
	if (e68_get_iml (sim->cpu) == 7) {
#ifdef DEBUG_SONY
		mac_log_deb ("sony: check aborted (iml=7)\n");
#endif
		return;
	}

	a7 = e68_get_areg32 (sim->cpu, 7);
	e68_set_mem32 (sim->cpu, a7 - 4, e68_get_pc (sim->cpu));
	e68_set_areg32 (sim->cpu, 7, a7 - 4);

	e68_set_pc_prefetch (sim->cpu, sim->sony.check_addr);
}

void mac_interrupt_osi (void *ext, unsigned char val)
{
	macplus_t *sim = ext;

	if (val) {
		if (mac_sony_check (&sim->sony)) {
			mac_interrupt_sony_check (sim);
		}

		e6522_set_ca2_inp (&sim->via, 0);
		e6522_set_ca2_inp (&sim->via, 1);
	}
}

static
void mac_set_reset (void *ext, unsigned char val)
{
	if (val == 0) {
		return;
	}

	mac_reset (ext);
}

static
void mac_set_vbuf (macplus_t *sim, unsigned long addr)
{
	unsigned char *vbuf;
	mem_blk_t     *blk;

	if (addr < mem_blk_get_size (sim->ram)) {
		vbuf = mem_blk_get_data (sim->ram) + addr;
	}
	else {
		blk = mem_get_blk (sim->mem, addr);

		if (blk == NULL) {
			mac_video_set_vbuf (sim->video, NULL);
			return;
		}

		vbuf = mem_blk_get_data (blk);
		vbuf += mem_blk_get_addr (blk) - sim->vbuf1;
	}

	mac_video_set_vbuf (sim->video, vbuf);
}

static
void mac_check_mouse (macplus_t *sim)
{
	if (sim->adb != NULL) {
		return;
	}

	if ((sim->mouse_delta_x <= -2) || (sim->mouse_delta_x >= 2)) {
		if (sim->dcd_a) {
			sim->via_port_b &= ~0x10;
		}
		else {
			sim->via_port_b |= 0x10;
		}

		if (sim->mouse_delta_x > 0) {
			sim->via_port_b ^= 0x10;
			sim->mouse_delta_x -= 2;
		}
		else {
			sim->mouse_delta_x += 2;
		}

		e6522_set_irb_inp (&sim->via, sim->via_port_b);
		e8530_set_dcd_a (&sim->scc, sim->dcd_a);

		sim->dcd_a = !sim->dcd_a;
	}

	if ((sim->mouse_delta_y <= -2) || (sim->mouse_delta_y >= 2)) {
		if (sim->dcd_b) {
			sim->via_port_b &= ~0x20;
		}
		else {
			sim->via_port_b |= 0x20;
		}

		if (sim->mouse_delta_y > 0) {
			sim->mouse_delta_y -= 2;
		}
		else {
			sim->via_port_b ^= 0x20;
			sim->mouse_delta_y += 2;
		}

		e6522_set_irb_inp (&sim->via, sim->via_port_b);
		e8530_set_dcd_b (&sim->scc, sim->dcd_b);

		sim->dcd_b = !sim->dcd_b;
	}
}

static
void mac_set_mouse (void *ext, int dx, int dy, unsigned but)
{
	macplus_t     *sim = ext;
	unsigned char old;

	if (sim->pause) {
		if (but) {
			mac_set_pause (sim, 0);
		}

		return;
	}

	if ((sim->mouse_button ^ but) & ~but & 4) {
		mac_set_msg (sim, "term.release", "1");
	}

	sim->mouse_button = but;

	if (sim->adb_mouse != NULL) {
		adb_mouse_move (sim->adb_mouse, but, dx, dy);
		return;
	}

	old = sim->via_port_b;

	if (but & 1) {
		sim->via_port_b &= 0xf7;
	}
	else {
		sim->via_port_b |= 0x08;
	}

	if (sim->via_port_b != old) {
		e6522_set_irb_inp (&sim->via, sim->via_port_b);
	}

	sim->mouse_delta_x += dx;
	sim->mouse_delta_y += dy;
}

static
void mac_set_key (void *ext, unsigned event, pce_key_t key)
{
	macplus_t *sim = ext;

	if (event == PCE_KEY_EVENT_MAGIC) {
		mac_set_hotkey (sim, key);
		return;
	}

	if (sim->kbd != NULL) {
		mac_kbd_set_key (sim->kbd, event, key);
	}

	if (sim->adb_kbd != NULL) {
		adb_kbd_set_key (sim->adb_kbd, event, key);
	}
}

static
void mac_set_adb_int (void *ext, unsigned char val)
{
	macplus_t *sim = ext;

	if (val) {
		sim->via_port_b &= ~0x08;
	}
	else {
		sim->via_port_b |= 0x08;
	}

	e6522_set_irb_inp (&sim->via, sim->via_port_b);
}

static
void mac_set_iwm_motor (void *ext, unsigned char val)
{
	macplus_t *sim = ext;

	mac_set_speed (sim, PCE_MAC_SPEED_IWM, val ? 4 : 0);
}

static
void mac_set_rtc_data (void *ext, unsigned char val)
{
	macplus_t *sim = ext;

	if (val) {
		sim->via_port_b |= 0x01;
	}
	else {
		sim->via_port_b &= ~0x01;
	}

	e6522_set_irb_inp (&sim->via, sim->via_port_b);
}

static
void mac_set_via_port_a (void *ext, unsigned char val)
{
	macplus_t     *sim = ext;
	unsigned char old;

	if (sim->via_port_a == val) {
		return;
	}

#ifdef DEBUG_VIA
	mac_log_deb ("via: set port a: %02X\n", val);
#endif

	old = sim->via_port_a;
	sim->via_port_a = val;

	if ((old ^ val) & 0x10) {
		if (sim->model & PCE_MAC_PLUS) {
			mac_set_overlay (sim, (val & 0x10) != 0);
		}
		else if (sim->model & (PCE_MAC_SE | PCE_MAC_CLASSIC)) {
			mac_iwm_set_drive_sel (&sim->iwm, (val & 0x10) != 0);
		}
	}

	if ((old ^ val) & 0x20) {
		mac_iwm_set_head_sel (&sim->iwm, (val & 0x20) != 0);
	}

	if ((old ^ val) & 0x40) {
		unsigned long addr;

		if (val & 0x40) {
			mac_log_deb ("main video buffer\n");
			addr = sim->vbuf1;
		}
		else {
			mac_log_deb ("alternate video buffer\n");
			addr = sim->vbuf2;
		}

		mac_set_vbuf (sim, addr);
	}

	if ((old ^ val) & 0x08) {
		if (sim->model & PCE_MAC_PLUS) {
			unsigned char *sbuf;

			sbuf = mem_blk_get_data (sim->ram);

			if (val & 0x08) {
				mac_log_deb ("main sound buffer\n");
				sbuf += sim->sbuf1;
			}
			else {
				mac_log_deb ("alternate sound buffer\n");
				sbuf += sim->sbuf2;
			}

			mac_sound_set_sbuf (&sim->sound, sbuf);
		}
	}

	if ((old ^ val) & 0x07) {
		mac_sound_set_volume (&sim->sound, val & 7);
	}
}

static
void mac_set_via_port_b (void *ext, unsigned char val)
{
	macplus_t     *sim = ext;
	unsigned char old;

	if (sim->via_port_b == val) {
		return;
	}

#ifdef DEBUG_VIA
	mac_log_deb ("via: set port b: %02X\n", val);
#endif

	old = sim->via_port_b;
	sim->via_port_b = val;

	mac_rtc_set_uint8 (&sim->rtc, val);

	if ((old ^ val) & 0x80) {
		mac_sound_set_enable (&sim->sound, (val & 0x80) == 0);
	}

	if (sim->adb != NULL) {
		mac_adb_set_state (sim->adb, (val >> 4) & 3);
	}
}

static
unsigned char mac_scc_get_uint8 (void *ext, unsigned long addr)
{
	unsigned char val;
	macplus_t     *sim = ext;

	val = 0xff;

	switch (addr) {
	case 0x1ffff8:
		val = e8530_get_ctl_b (&sim->scc);
		break;

	case 0x1ffffa:
		val = e8530_get_ctl_a (&sim->scc);
		break;

	case 0x1ffffc:
		val = e8530_get_data_b (&sim->scc);
		break;

	case 0x1ffffe:
		val = e8530_get_data_a (&sim->scc);
		break;
	}

#ifdef DEBUG_SCC
	mac_log_deb ("scc: get  8: %06lX -> %02X\n", addr, val);
#endif

	return (val);
}

static
void mac_scc_set_uint8 (void *ext, unsigned long addr, unsigned char val)
{
	macplus_t *sim = ext;

#ifdef DEBUG_SCC
	mac_log_deb ("scc: set  8: %06lX <- %02X\n", addr, val);
#endif

	switch (addr) {
	case 0x3ffff9:
		e8530_set_ctl_b (&sim->scc, val);
		break;

	case 0x3ffffb:
		e8530_set_ctl_a (&sim->scc, val);
		break;

	case 0x3ffffd:
		e8530_set_data_b (&sim->scc, val);
		break;

	case 0x3fffff:
		e8530_set_data_a (&sim->scc, val);
		break;
	}
}


static
void mac_setup_system (macplus_t *sim)
{
	pce_log_tag (MSG_INF, "SYSTEM:", "model=%s memtest=%d\n",
		SYSTEM_MODEL, SYSTEM_MEMTEST
	);

	sim->model = PCE_MAC_PLUS;
	sim->memtest = (SYSTEM_MEMTEST != 0);
}

static
void mac_setup_mem (macplus_t *sim)
{
	sim->mem = mem_new();

	mem_set_fct (sim->mem, sim,
		mac_mem_get_uint8, mac_mem_get_uint16, mac_mem_get_uint32,
		mac_mem_set_uint8, mac_mem_set_uint16, mac_mem_set_uint32
	);

	ini_get_ram (sim->mem, &sim->ram);
	ini_get_rom (sim->mem);

	sim->ram = mem_get_blk (sim->mem, 0x00000000);
	sim->rom = mem_get_blk (sim->mem, 0x00400000);

	sim->ram_ovl = NULL;
	sim->rom_ovl = NULL;

	if (sim->ram == NULL) {
		pce_log (MSG_ERR, "*** RAM not found at 000000\n");
		return;
	}

	if (sim->rom == NULL) {
		pce_log (MSG_ERR, "*** ROM not found at 400000\n");
		return;
	}

	sim->ram_ovl = mem_blk_clone (sim->ram);
	mem_blk_set_addr (sim->ram_ovl, 0x00600000);

	if (mem_blk_get_size (sim->ram_ovl) > 0x00200000) {
		mem_blk_set_size (sim->ram_ovl, 0x00200000);
	}

	sim->rom_ovl = mem_blk_clone (sim->rom);
	mem_blk_set_addr (sim->rom_ovl, 0);

	sim->overlay = 0;

	if (sim->memtest == 0) {
		pce_log_tag (MSG_INF, "RAM:", "disabling memory test\n");

		if (sim->model & PCE_MAC_PLUS) {
			mem_set_uint32_be (sim->mem, 0x02ae, 0x00400000);
		}
		else if (sim->model & (PCE_MAC_SE | PCE_MAC_CLASSIC)) {
			mem_set_uint32_be (sim->mem, 0x0cfc, 0x574c5343);
		}
	}
}

static
void mac_setup_cpu (macplus_t *sim)
{
	pce_log_tag (MSG_INF, "CPU:", "model=%s speed=%d\n", CPU_MODEL, CPU_SPEED);

	sim->cpu = e68_new();
	if (sim->cpu == NULL) {
		return;
	}

	if (mac_set_cpu_model (sim, CPU_MODEL)) {
		pce_log (MSG_ERR, "*** unknown cpu model (%s)\n", CPU_MODEL);
	}

	e68_set_mem_fct (sim->cpu, sim->mem,
		&mem_get_uint8,
		&mem_get_uint16_be,
		&mem_get_uint32_be,
		&mem_set_uint8,
		&mem_set_uint16_be,
		&mem_set_uint32_be
	);

	e68_set_reset_fct (sim->cpu, sim, mac_set_reset);

	e68_set_hook_fct (sim->cpu, sim, mac_hook);

	e68_set_address_check (sim->cpu, 0);

	sim->speed_factor = CPU_SPEED;
	sim->speed_limit[PCE_MAC_SPEED_USER] = CPU_SPEED;
}

static
void mac_setup_via (macplus_t *sim)
{
	const unsigned long addr = 0x00efe000;
	const unsigned long size = 16 * 512;

	pce_log_tag (MSG_INF, "VIA:", "addr=0x%06lx size=0x%lx\n", addr, size);

	sim->via_port_a = 0;
	sim->via_port_b = 0;

	e6522_init (&sim->via, 9);

	e6522_set_irq_fct (&sim->via, sim, mac_interrupt_via);

	e6522_set_ora_fct (&sim->via, sim, mac_set_via_port_a);
	e6522_set_orb_fct (&sim->via, sim, mac_set_via_port_b);

	mem_blk_t *blk = mem_blk_new (addr, size, 0);

	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, &sim->via,
		e6522_get_uint8, e6522_get_uint16, e6522_get_uint32,
		e6522_set_uint8, e6522_set_uint16, e6522_set_uint32
	);

	mem_add_blk (sim->mem, blk, 1);
}

static
void mac_setup_scc (macplus_t *sim)
{
	mem_blk_t     *blk;
	const unsigned long addr = 0x800000;
	const unsigned long size = 0x400000;

	pce_log_tag (MSG_INF, "SCC:", "addr=0x%06lx size=0x%lx\n", addr, size);

	e8530_init (&sim->scc);
	e8530_set_irq_fct (&sim->scc, sim, mac_interrupt_scc);
	e8530_set_clock (&sim->scc, 3672000, 3672000, 3672000);

	blk = mem_blk_new (addr, size, 0);
	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, sim,
		mac_scc_get_uint8, NULL, NULL,
		mac_scc_set_uint8, NULL, NULL
	);

	mem_add_blk (sim->mem, blk, 1);
}

static
void mac_setup_serial (macplus_t *sim)
{
	mac_ser_init (&sim->ser[0]);
	mac_ser_set_scc (&sim->ser[0], &sim->scc, 0);

	mac_ser_init (&sim->ser[1]);
	mac_ser_set_scc (&sim->ser[1], &sim->scc, 1);

	e8530_set_multichar (&sim->scc, 0, 1, 1);
	e8530_set_multichar (&sim->scc, 1, 1, 1);

	if (mac_ser_set_driver (&sim->ser[0], SERIAL0_DRIVER)) {
		pce_log (MSG_ERR, "*** can't open serial driver 0\n");
	}
	if (mac_ser_set_driver (&sim->ser[1], SERIAL1_DRIVER)) {
		pce_log (MSG_ERR, "*** can't open serial driver 1\n");
	}
}

static
void mac_setup_rtc (macplus_t *sim)
{
	// TODO find place for rtc file
	sim->rtc_fname = RTC_FILE;

	mac_rtc_init (&sim->rtc);

	mac_rtc_set_data_fct (&sim->rtc, sim, mac_set_rtc_data);
	mac_rtc_set_osi_fct (&sim->rtc, sim, mac_interrupt_osi);

	mac_rtc_set_realtime (&sim->rtc, 1);

	if (mac_rtc_load_file (&sim->rtc, RTC_FILE)) {
		pce_log (MSG_ERR, "*** reading rtc file failed\n");
	}

	if (RTC_ROMDISK) {
		sim->rtc.data[0x78] = 0x00;
		sim->rtc.data[0x79] = 0x06;
		sim->rtc.data[0x7a] = 0xff;
		sim->rtc.data[0x7b] = 0xcb;
	}

	#ifdef RTC_APPLETALK
		#if (RTC_APPLETALK == 0)
			sim->rtc.data[0x13] = 0x22;
		#elif (RTC_APPLETALK == 1)
			sim->rtc.data[0x13] = 0x21;
		#endif
	#endif

	#ifdef RTC_START
		mac_rtc_set_time_str (&sim->rtc, RTC_START);
	#else
		mac_rtc_set_time_now (&sim->rtc);
	#endif
}

static
void mac_setup_kbd (macplus_t *sim)
{
	sim->kbd = NULL;

	if ((sim->model & PCE_MAC_PLUS) == 0) {
		return;
	}

	pce_log_tag (MSG_INF,
		"KEYBOARD:", "model=%u international=%d keypad=%s\n",
		KEYBOARD_MODEL,
		KEYBOARD_INTL,
		KEYBOARD_KEYPAD_MOTION ? "motion" : "keypad"
	);

	sim->kbd = mac_kbd_new();

	if (sim->kbd == NULL) {
		return;
	}

	mac_kbd_set_model (sim->kbd, KEYBOARD_MODEL, KEYBOARD_INTL);
	mac_kbd_set_keypad_mode (sim->kbd, KEYBOARD_KEYPAD_MOTION);
	mac_kbd_set_data_fct (sim->kbd, &sim->via, e6522_set_shift_inp);
	mac_kbd_set_intr_fct (sim->kbd, sim, mac_interrupt);

	e6522_set_shift_out_fct (&sim->via, sim->kbd, mac_kbd_set_uint8);
	e6522_set_cb2_fct (&sim->via, sim->kbd, mac_kbd_set_data);
}

static
void mac_setup_adb (macplus_t *sim)
{
	sim->adb = NULL;
	sim->adb_mouse = NULL;
	sim->adb_kbd = NULL;

	if ((sim->model & (PCE_MAC_SE | PCE_MAC_CLASSIC)) == 0) {
		return;
	}

	pce_log_tag (MSG_INF, "ADB:", "enabled\n");

	sim->adb = mac_adb_new();

	if (sim->adb == NULL) {
		pce_log (MSG_ERR, "*** can't create adb\n");
		return;
	}

	adb_set_shift_in_fct (sim->adb, &sim->via, e6522_shift_in);
	adb_set_shift_out_fct (sim->adb, &sim->via, e6522_shift_out);

	adb_set_int_fct (sim->adb, sim, mac_set_adb_int);

	if (ADB_MOUSE) {
		pce_log_tag (MSG_INF, "ADB:", "mouse\n");

		sim->adb_mouse = adb_mouse_new();

		if (sim->adb_mouse != NULL) {
			adb_add_device (sim->adb, &sim->adb_mouse->dev);
		}
	}

	if (ADB_KEYBOARD) {
		pce_log_tag (MSG_INF, "ADB:",
			"keyboard keypad_mode=%s\n",
			ADB_KEYPAD_MOTION ? "motion" : "keypad"
		);

		sim->adb_kbd = adb_kbd_new();

		if (sim->adb_kbd != NULL) {
			adb_kbd_set_keypad_mode (sim->adb_kbd, ADB_KEYPAD_MOTION);
			adb_add_device (sim->adb, &sim->adb_kbd->dev);
		}
	}
}

static
void mac_setup_disks (macplus_t *sim)
{
	disks_t   *dsks;
	disk_t    *dsk = NULL;

	dsks = dsks_new();

	#ifdef SDL_SIM
		dsk = dsk_img_open(DISK_FILE_NAME, 0, 0);
	#else
		dsk = flash_disk_init(DISK_PARTITION_NAME, 0);
	#endif

	if (dsk == NULL) {
		pce_log_tag (MSG_ERR, "DISK:", "couldn't get disk\n");
		return;
	}

	dsk_set_drive (dsk, DISK_DRIVE);

	pce_log_tag (MSG_INF,
		"DISK:", "drive=%u type=image blocks=%lu chs=%lu/%lu/%lu %s %s\n",
		dsk->drive,
		(unsigned long) dsk->blocks,
		(unsigned long) dsk->c,
		(unsigned long) dsk->h,
		(unsigned long) dsk->s,
		(dsk->readonly ? "ro" : "rw"),
		dsk->fname
	);

	dsks_add_disk (dsks, dsk);
	sim->dsks = dsks;
}

static
void mac_setup_iwm (macplus_t *sim)
{
	mac_iwm_init (&sim->iwm);
	mac_iwm_set_motor_fct (&sim->iwm, sim, mac_set_iwm_motor);
	mac_iwm_set_disks (&sim->iwm, sim->dsks);

	pce_log_tag (MSG_INF, "IWM:", "addr=0x%06lx\n", 0xd00000UL);

	mac_iwm_enable_pwm (&sim->iwm, 0, 1);
	mac_iwm_set_heads (&sim->iwm, 0, IWM_DRIVE0_SINGLE_SIDED ? 1 : 2);
	mac_iwm_set_disk_id (&sim->iwm, 0, IWM_DRIVE0_DISK);
	mac_iwm_set_auto_rotate (&sim->iwm, 0, IWM_DRIVE0_AUTO_ROTATE);
	if (IWM_DRIVE0_INSERTED) {
		mac_iwm_insert (&sim->iwm, 0);
	}

	mac_iwm_enable_pwm (&sim->iwm, 1, 1);
	mac_iwm_set_heads (&sim->iwm, 1, IWM_DRIVE1_SINGLE_SIDED ? 1 : 2);
	mac_iwm_set_disk_id (&sim->iwm, 1, IWM_DRIVE1_DISK);
	mac_iwm_set_auto_rotate (&sim->iwm, 1, IWM_DRIVE1_AUTO_ROTATE);
	if (IWM_DRIVE1_INSERTED) {
		mac_iwm_insert (&sim->iwm, 1);
	}
}

static
void mac_setup_scsi (macplus_t *sim)
{
	mem_blk_t     *blk;
	const unsigned long addr = 0x580000, size = 0x80000;

	pce_log_tag (MSG_INF, "SCSI:", "addr=0x%06lx size=0x%lx\n", addr, size);

	mac_scsi_init (&sim->scsi);

	if (sim->model & PCE_MAC_SE) {
		mac_scsi_set_int_fct (&sim->scsi, sim, mac_interrupt_scsi);
	}

	mac_scsi_set_disks (&sim->scsi, sim->dsks);

	blk = mem_blk_new (addr, size, 0);
	if (blk == NULL) {
		return;
	}

	mem_blk_set_fct (blk, &sim->scsi,
		mac_scsi_get_uint8, mac_scsi_get_uint16, NULL,
		mac_scsi_set_uint8, mac_scsi_set_uint16, NULL
	);

	mem_add_blk (sim->mem, blk, 1);

	mac_scsi_set_drive (&sim->scsi, SCSI_DEVICE0_ID, SCSI_DEVICE0_DRIVE);
	mac_scsi_set_drive_vendor (&sim->scsi, SCSI_DEVICE0_ID, SCSI_DEVICE0_VENDOR);
	mac_scsi_set_drive_product (&sim->scsi, SCSI_DEVICE0_ID, SCSI_DEVICE0_PRODUCT);
	pce_log_tag (MSG_INF,
		"SCSI:", "id=%u drive=%u vendor=\"%s\" product=\"%s\"\n",
		SCSI_DEVICE0_ID, SCSI_DEVICE0_DRIVE, SCSI_DEVICE0_VENDOR, SCSI_DEVICE0_PRODUCT
	);
}

static
void mac_setup_sony (macplus_t *sim)
{
	mac_sony_init (&sim->sony, 0);
	mac_sony_set_mem (&sim->sony, sim->mem);
	mac_sony_set_disks (&sim->sony, sim->dsks);
}

static
void mac_setup_sound (macplus_t *sim)
{
	unsigned long addr;
	unsigned long freq=6000;
	#ifdef SDL_SIM
		const char    *driver="SDL";
	#else
		const char    *driver="NONE";
	#endif

	mac_sound_init (&sim->sound);

	if (sim->ram == NULL) {
		return;
	}

	addr = mem_blk_get_size (sim->ram);
	addr = (addr < 0x300) ? 0 : (addr - 0x300);

	pce_log_tag (MSG_INF, "SOUND:", "addr=0x%06lX lowpass=%lu driver=%s\n",
		addr, freq,
		(driver != NULL) ? driver : "<none>"
	);

	sim->sbuf1 = addr;
	sim->sbuf2 = addr - 0x5c00;

	mac_sound_set_sbuf (&sim->sound, mem_blk_get_data (sim->ram) + sim->sbuf1);

	mac_sound_set_lowpass (&sim->sound, freq);

	if (mac_sound_set_driver (&sim->sound)) {
		pce_log (MSG_ERR, "*** setting sound driver failed\n");
	}
}

static
void mac_setup_terminal (macplus_t *sim)
{
	sim->trm = ini_get_terminal (par_terminal);

	if (sim->trm == NULL) {
		return;
	}

	trm_set_escape_str (sim->trm, TERMINAL_ESCAPE);
	trm_set_scale (sim->trm, TERMINAL_SCALE);
	trm_set_min_size (sim->trm, 512, 384);
	trm_set_aspect_ratio (sim->trm, TERMINAL_ASPECT_X, TERMINAL_ASPECT_Y);
	trm_set_mouse_scale (sim->trm, TERMINAL_MOUSE_MUL_X, TERMINAL_MOUSE_DIV_X, TERMINAL_MOUSE_MUL_Y, TERMINAL_MOUSE_DIV_Y);

	trm_set_msg_fct (sim->trm, sim, mac_set_msg);
	trm_set_key_fct (sim->trm, sim, mac_set_key);
	trm_set_mouse_fct (sim->trm, sim, mac_set_mouse);
}

static
void mac_setup_video (macplus_t *sim)
{
	unsigned long addr1;
	unsigned      i;

	if (sim->ram == NULL) {
		return;
	}

	addr1 = mem_blk_get_size (sim->ram);
	addr1 = (addr1 < 0x5900) ? 0 : (addr1 - 0x5900);

	pce_log_tag (MSG_INF, "VIDEO:", "addr=0x%06lX w=%u h=%u bright=%u%%\n",
		addr1, VIDEO_W, VIDEO_H, VIDEO_BRIGHTNESS / 10
	);

	sim->vbuf1 = addr1;

	if (addr1 >= 0x8000) {
		sim->vbuf2 = addr1 - 0x8000;
	}
	else {
		sim->vbuf2 = addr1;
	}

	sim->video = mac_video_new (VIDEO_W, VIDEO_H);

	if (sim->video == NULL) {
		return;
	}

	mac_video_set_vbi_fct (sim->video, sim, mac_interrupt_vbi);

	mac_set_vbuf (sim, sim->vbuf1);

	if (sim->trm != NULL) {
		mac_video_set_terminal (sim->video, sim->trm);

		trm_open (sim->trm, 512, 342);
	}

	mac_video_set_color (sim->video, VIDEO_COLOR0, VIDEO_COLOR1);
	mac_video_set_brightness (sim->video, (255UL * VIDEO_BRIGHTNESS + 500) / 1000);

	for (i = 0; i < (VIDEO_W / 8) * VIDEO_H; i++) {
		mem_set_uint8 (sim->mem, sim->vbuf1 + i, 0xff);
	}
}

void mac_init (macplus_t *sim)
{
	unsigned i;

	sim->trm = NULL;
	sim->video = NULL;

	sim->reset = 0;

	sim->disk_id = 1;

	sim->dcd_a = 0;
	sim->dcd_b = 0;

	sim->mouse_delta_x = 0;
	sim->mouse_delta_y = 0;
	sim->mouse_button = 0;

	sim->intr = 0;

	sim->pause = 0;
	sim->brk = 0;

	sim->speed_factor = 1;
	sim->speed_limit[0] = 1;
	sim->speed_clock_extra = 0;
	sim->sync_sleep = 0;

	for (i = 1; i < PCE_MAC_SPEED_CNT; i++) {
		sim->speed_limit[i] = 0;
	}

	sim->ser_clk = 0;
	sim->clk_cnt = 0;

	for (i = 0; i < 4; i++) {
		sim->clk_div[i] = 0;
	}

	bps_init (&sim->bps);

	mac_setup_system (sim);
	mac_setup_mem (sim);
	mac_setup_cpu (sim);
	mac_setup_via (sim);
	mac_setup_scc (sim);
	mac_setup_serial (sim);
	mac_setup_rtc (sim);
	mac_setup_kbd (sim);
	mac_setup_adb (sim);
	mac_setup_disks (sim);
	mac_setup_iwm (sim);
	mac_setup_scsi (sim);
	mac_setup_sony (sim);
	mac_setup_sound (sim);
	mac_setup_terminal (sim);
	mac_setup_video (sim);

	trm_set_msg_trm (sim->trm, "term.title", "pce-macplus");

	mac_clock_discontinuity (sim);
}

macplus_t *mac_new ()
{
	macplus_t *sim;

	sim = malloc (sizeof (macplus_t));
	if (sim == NULL) {
		return (NULL);
	}

	mac_init (sim);

	return (sim);
}

void mac_free (macplus_t *sim)
{
	if (sim == NULL) {
		return;
	}

	if (mac_rtc_save_file (&sim->rtc, sim->rtc_fname)) {
		pce_log (MSG_ERR, "*** writing rtc file failed (%s)\n",
			sim->rtc_fname
		);
	}

	mac_video_del (sim->video);
	trm_del (sim->trm);
	mac_sound_free (&sim->sound);
	mac_sony_free (&sim->sony);
	mac_scsi_free (&sim->scsi);
	mac_iwm_free (&sim->iwm);
	dsks_del (sim->dsks);
	mac_adb_del (sim->adb);
	mac_kbd_del (sim->kbd);
	mac_rtc_free (&sim->rtc);
	mac_ser_free (&sim->ser[1]);
	mac_ser_free (&sim->ser[0]);
	e8530_free (&sim->scc);
	e6522_free (&sim->via);
	e68_del (sim->cpu);
	mem_del (sim->mem);

	mem_blk_del (sim->ram_ovl);
	mem_blk_del (sim->rom_ovl);

	bps_free (&sim->bps);
}

void mac_del (macplus_t *sim)
{
	if (sim != NULL) {
		mac_free (sim);
		free (sim);
	}
}


unsigned long long mac_get_clkcnt (macplus_t *sim)
{
	return (sim->clk_cnt);
}

void mac_clock_discontinuity (macplus_t *sim)
{
	sim->sync_clk = 0;
	sim->sync_us = 0;
	pce_get_interval_us (&sim->sync_us);

	sim->speed_clock_extra = 0;
}

void mac_set_pause (macplus_t *sim, int pause)
{
	sim->pause = (pause != 0);

	if (sim->pause == 0) {
		mac_clock_discontinuity (sim);
	}
}

static
void mac_adjust_speed (macplus_t *sim)
{
	unsigned i, v;

	v = 0;

	for (i = 0; i < PCE_MAC_SPEED_CNT; i++) {
		if (sim->speed_limit[i] > 0) {
			if ((v == 0) || (sim->speed_limit[i] < v)) {
				v = sim->speed_limit[i];
			}
		}
	}

	if (v == sim->speed_factor) {
		return;
	}

	mac_log_deb ("speed: %u\n", v);

	mac_rtc_set_realtime (&sim->rtc, (v != 1));

	sim->speed_factor = v;
	sim->speed_clock_extra = 0;

	mac_clock_discontinuity (sim);
}

void mac_set_speed (macplus_t *sim, unsigned idx, unsigned factor)
{
	if (idx >= PCE_MAC_SPEED_CNT) {
		return;
	}

	sim->speed_limit[idx] = factor;

	mac_adjust_speed (sim);
}

int mac_set_msg_trm (macplus_t *sim, const char *msg, const char *val)
{
	if (sim->trm == NULL) {
		return (1);
	}

	return (trm_set_msg_trm (sim->trm, msg, val));
}

int mac_set_cpu_model (macplus_t *sim, const char *model)
{
	if (strcmp (model, "68000") == 0) {
		e68_set_68000 (sim->cpu);
	}
	else if (strcmp (model, "68010") == 0) {
		e68_set_68010 (sim->cpu);
	}
	else if (strcmp (model, "68020") == 0) {
		e68_set_68020 (sim->cpu);
	}
	else {
		return (1);
	}

	return (0);
}

void mac_reset (macplus_t *sim)
{
	if (sim->reset) {
		return;
	}

	sim->reset = 1;

	mac_log_deb ("mac: reset\n");

	sim->dcd_a = 0;
	sim->dcd_b = 0;

	sim->mouse_delta_x = 0;
	sim->mouse_delta_y = 0;
	sim->mouse_button = 0;

	sim->intr = 0;
	sim->intr_scsi_via = 0;

	if (sim->model & PCE_MAC_PLUS) {
		mac_set_overlay (sim, 1);
	}
	else {
		mac_set_overlay (sim, 0);
		if ((sim->rom != NULL) && (sim->rom->size >= 8)) {
			e68_set_mem32 (sim->cpu, 0, mem_blk_get_uint32_be (sim->rom, 0));
			e68_set_mem32 (sim->cpu, 4, mem_blk_get_uint32_be (sim->rom, 4));
		}
	}

	e6522_reset (&sim->via);

	sim->via_port_a = 0xf7;
	sim->via_port_b = 0xff;

	e6522_set_ira_inp (&sim->via, sim->via_port_a);
	e6522_set_irb_inp (&sim->via, sim->via_port_b);

	mac_sony_reset (&sim->sony);
	mac_scsi_reset (&sim->scsi);
	e8530_reset (&sim->scc);

	if (sim->adb != NULL) {
		adb_reset (sim->adb);
	}

	e68_reset (sim->cpu);

	mac_clock_discontinuity (sim);

	sim->reset = 0;
}

static
void mac_realtime_sync (macplus_t *sim, unsigned long n)
{
	unsigned long us1, us2;

	sim->sync_clk += n;

	if (sim->sync_clk >= (MAC_CPU_CLOCK / MAC_CPU_SYNC)) {
		sim->sync_clk -= (MAC_CPU_CLOCK / MAC_CPU_SYNC);

		us1 = pce_get_interval_us (&sim->sync_us);
		us2 = (1000000 / MAC_CPU_SYNC);

		if (us1 < us2) {
			sim->sync_sleep += us2 - us1;

			if (sim->sync_sleep > 0) {
				sim->speed_clock_extra += 1;
			}
		}
		else {
			sim->sync_sleep -= us1 - us2;

			if (sim->sync_sleep < 0) {
				if (sim->speed_clock_extra > 0) {
					sim->speed_clock_extra -= 1;
				}
			}
		}

		if (sim->sync_sleep >= MAC_CPU_SLEEP) {
			pce_usleep (sim->sync_sleep);
		}

		if (sim->sync_sleep < -1000000) {
			mac_log_deb ("system too slow, skipping 1 second\n");
			sim->sync_sleep += 1000000;
		}
	}
}

void mac_clock_scc (macplus_t *sim, unsigned n)
{
	/* 3.672 MHz = (15/32 * 7.8336 MHz) */
	sim->ser_clk += 15 * n;

	if (sim->ser_clk >= 32) {
		e8530_clock (&sim->scc, sim->ser_clk / 32);
		sim->ser_clk &= 31;
	}
}

void mac_clock (macplus_t *sim, unsigned n)
{
	unsigned long viaclk, clkdiv, cpuclk;

	if (n == 0) {
		n = sim->cpu->delay;
		if (n == 0) {
			n = 1;
		}
	}

	if (sim->speed_factor == 0) {
		cpuclk = n + sim->speed_clock_extra;
		clkdiv = 1;
	}
	else {
		cpuclk = n;
		clkdiv = sim->speed_factor;
	}

	e68_clock (sim->cpu, cpuclk);

	mac_sound_clock (&sim->sound, cpuclk);

	sim->clk_cnt += n;

	sim->clk_div[0] += n;

	while (sim->clk_div[0] >= clkdiv) {
		sim->clk_div[1] += 1;
		sim->clk_div[0] -= clkdiv;
	}

	if (sim->clk_div[1] < 10) {
		return;
	}

	viaclk = sim->clk_div[1] / 10;

	e6522_clock (&sim->via, viaclk);

	if (sim->adb != NULL) {
		mac_adb_clock (sim->adb, 10 * viaclk);
	}

	mac_iwm_clock (&sim->iwm, viaclk);

	mac_clock_scc (sim, 10 * viaclk);

	sim->clk_div[1] -= 10 * viaclk;
	sim->clk_div[2] += 10 * viaclk;

	if (sim->clk_div[2] < 256) {
		return;
	}

	mac_video_clock (sim->video, sim->clk_div[2]);

	mac_ser_process (&sim->ser[0]);
	mac_ser_process (&sim->ser[1]);

	if (sim->kbd != NULL) {
		mac_kbd_clock (sim->kbd, sim->clk_div[2]);
	}

	sim->clk_div[3] += sim->clk_div[2];
	sim->clk_div[2] = 0;

	if (sim->clk_div[3] < 8192) {
		return;
	}

	if (sim->trm != NULL) {
		trm_check (sim->trm);
	}

	mac_check_mouse (sim);

	mac_rtc_clock (&sim->rtc, sim->clk_div[3]);

	mac_realtime_sync (sim, sim->clk_div[3]);

	sim->clk_div[3] = 0;
}

void print_version (void)
{
	fputs (
		"pce-macplus version " PCE_VERSION_STR
		"\n\n"
		"Copyright (C) 2007-" PCE_YEAR " Hampa Hug <hampa@hampa.ch>\n",
		stdout
	);

	fflush (stdout);
}

void mac_log_banner (void)
{
	pce_log_inf (
		"pce-macplus version " PCE_VERSION_STR "\n"
		"Copyright (C) 2007-" PCE_YEAR " Hampa Hug <hampa@hampa.ch>\n"
	);
}

void mac_log_deb (const char *msg, ...)
{
	va_list       va;
	unsigned long pc;

	if (par_sim != NULL) {
		pc = e68_get_last_pc (par_sim->cpu, 0);
	}
	else {
		pc = 0;
	}

	pce_log (MSG_DEB, "[%06lX] ", pc & 0xffffff);

	va_start (va, msg);
	pce_log_va (MSG_DEB, msg, va);
	va_end (va);
}

int cmd_get_sym_mac(macplus_t *sim, const char *sym, unsigned long *val)
{
	if (e68_get_reg (sim->cpu, sym, val) == 0) {
		return (0);
	}

	return (1);
}

int cmd_set_sym_mac(macplus_t *sim, const char *sym, unsigned long val)
{
	if (e68_set_reg (sim->cpu, sym, val) == 0) {
		return (0);
	}

	return (1);
}
