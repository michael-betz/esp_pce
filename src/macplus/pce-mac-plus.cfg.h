#pragma once
#include <stdbool.h>

// The Macintosh model to emulate. Valid models are:
// mac-plus:    A Macintosh 128K, 512K, 512Ke or Plus
// mac-se:      A Macintosh SE or SE-FDHD
// mac-classic: A Macintosh Classic
#define SYSTEM_MODEL "mac-plus"

// Enable or disable the startup memory test.
#define SYSTEM_MEMTEST 0


// The CPU model. Valid models are "68000" and "68010".
#define CPU_MODEL "68000"

// The CPU speed multiplier. A value of 1 emulates a
// 7.8336 MHz CPU. A higher value emulates a faster CPU
// but also takes up more host CPU time. A value of 0
// dynamically adjusts the CPU speed.
#define CPU_SPEED 1


// Multiple "ram" sections may be present.
// The base address
#define RAM_ADDRESS 0

// The memory block size
#define RAM_SIZE (2048 * 1024)

// The memory block is initialized with this value.
#define RAM_DEFAULT 0x00


// SDL only: Rom is loaded from this file
#define ROM_FILE_NAME "rom.bin"

// ESP only: Rom is loaded from partition with type 0x40 and subtype 0x01
#define ROM_PARTITION_NAME "rom"

// The base address
#define ROM_ADDRESS 0x400000

// The rom size
#define ROM_SIZE (256 * 1024)


// SDL only
#define DISK_FILE_NAME "hd7.img"

// ESP only
#define DISK_PARTITION_NAME "hd"

// Need to match SCSI_DEVICE<N>_DRIVE
#define DISK_DRIVE 128


// Multiple "terminal" sections may be present. The first
// one will be used unless a terminal type is specified
// on the command line.
#define TERMINAL_DRIVER "sdl"

// The terminal escape key. The default is "ESC".
#define TERMINAL_ESCAPE "ESC"

// The terminal scale factor. Only integral values are
// allowed.
#define TERMINAL_SCALE 1

// The terminal aspect ratio.
#define TERMINAL_ASPECT_X 3
#define TERMINAL_ASPECT_Y 2

// Add a border around the image
#define TERMINAL_BORDER 0

// Start in fullscreen mode.
#define TERMINAL_FULLSCREEN 0

// The mouse speed. The host mouse speed is multiplied by
// (mouse_mul_x / mouse_div_x) and (mouse_mul_y / mouse_div_y)
#define TERMINAL_MOUSE_MUL_X 1
#define TERMINAL_MOUSE_DIV_X 1
#define TERMINAL_MOUSE_MUL_Y 1
#define TERMINAL_MOUSE_DIV_Y 1

// Apply a low-pass filter with the specified cut-off
// frequency in Herz. This is separate from the low-pass
// filter in the sound driver. If the frequency is 0,
// the filter is disabled.
#define SOUND_LOWPASS 8000
#define SOUND_DRIVER "null"

// The model number and international flag are returned
// by the keyboard but MacOS seems to ignore them.
#define KEYBOARD_MODEL 0
#define KEYBOARD_INTL  0

// If keypad_motion is set to 1, host keypad keys are mapped
// to Macintosh motion keys.
#define KEYBOARD_KEYPAD_MOTION 0

// Enable the ADB mouse
#define ADB_MOUSE true

// Enable the ADB extended keyboard
#define ADB_KEYBOARD false

// Map keypad keys to motion keys
#define ADB_KEYPAD_MOTION false


// On startup the parameter RAM is loaded from this file. On
// shutdown it is written back.
#define RTC_FILE "pram-mac-plus.dat"

// The start time of the real time clock. If this parameter is
// not set, the real time clock is set to the current time.
#define RTC_START "1984-01-24 00:00:00"

// Set the startup disk to the ROM disk. This only works with
// the Macintosh Classic ROM.
#define RTC_ROMDISK 0

// #define RTC_APPLETALK 0


// Up to three IWM drives can be defined:
//   Drive 1: The internal drive
//   Drive 2: The external drive
//   Drive 3: The second (lower) internal drive in a Macintosh SE
// The IWM drives are only accessible if the replacement Sony driver
// is disabled above.
// The IWM drive number.
#define IWM_DRIVE0_DRIVE        1

// The disk that is inserted into this drive. This
// corresponds to a "disk" section below.
#define IWM_DRIVE0_DISK         1

// Insert the disk before the emulation starts, in order
// to boot from it.
#define IWM_DRIVE0_INSERTED     0

// Force the drive to be single sided.
#define IWM_DRIVE0_SINGLE_SIDED 0

// Automatically align the individual tracks.
#define IWM_DRIVE0_AUTO_ROTATE  1

#define IWM_DRIVE1_DRIVE        2
#define IWM_DRIVE1_DISK         2
#define IWM_DRIVE1_INSERTED     0
#define IWM_DRIVE1_SINGLE_SIDED 0
#define IWM_DRIVE1_AUTO_ROTATE  1

// The SCSI ID
#define SCSI_DEVICE0_ID 6
// The drive number. This number is used to identify
// a "disk" section. The number itself is meaningless.
#define SCSI_DEVICE0_DRIVE 128
#define SCSI_DEVICE0_VENDOR "PCE"
#define SCSI_DEVICE0_PRODUCT "PCEDISK"

// #define SCSI_DEVICE1_ID    4
// #define SCSI_DEVICE1_DRIVE 129
// #define SCSI_DEVICE1_VENDOR "PCE"
// #define SCSI_DEVICE1_PRODUCT "PCEDISK"

// #define SCSI_DEVICE2_ID    2
// #define SCSI_DEVICE2_DRIVE 130
// #define SCSI_DEVICE2_VENDOR "PCE"
// #define SCSI_DEVICE2_PRODUCT "PCEDISK"

// Up to multichar characters are sent or received
// without any transmission delay. For a real serial port
// this value is 1 but larger values can speed up
// transmission.
#define SERIAL0_MULTICHAR 1

// Not all character drivers are supported on
// all platforms.
#define SERIAL0_DRIVER "stdio:file=ser_a.out:flush=1"

#define SERIAL1_DRIVER "stdio:file=ser_b.out"


// The background color
#define VIDEO_COLOR0 0x000000

// The foreground color
#define VIDEO_COLOR1 0xffffff

// Brightness in the range 0 - 1000.
#define VIDEO_BRIGHTNESS 1000

#define VIDEO_W 512
#define VIDEO_H 342


// disk {
// 	drive    = 1
// 	type     = "auto"
// 	file     = "disk1.pri"
// 	file     = "disk1.image"
// 	file     = "disk1.img"
// 	optional = 1
// }

// disk {
// 	drive    = 2
// 	type     = "auto"
// 	file     = "disk2.pri"
// 	file     = "disk2.image"
// 	file     = "disk2.img"
// 	optional = 1
// }

// disk {
// 	drive    = 3
// 	type     = "auto"
// 	file     = "disk3.pri"
// 	file     = "disk3.image"
// 	file     = "disk3.img"
// 	optional = 1
// }

// disk {
// 	drive    = 128
// 	type     = "auto"
// 	file     = "hd1.img"
// 	optional = 0
// }

// disk {
// 	drive    = 129
// 	type     = "auto"
// 	file     = "hd2.img"
// 	optional = 1
// }

// disk {
// 	drive    = 130
// 	type     = "auto"
// 	file     = "hd3.img"
// 	optional = 1
// }
