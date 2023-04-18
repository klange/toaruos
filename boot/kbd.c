/**
 * @brief Keyboard reading functions.
 *
 * Abstracts away the differences between our EFI and BIOS
 * environments to provide consistent scancode feedback for
 * the menus and command line editor.
 *
 * For EFI, we use the WaitForKey and ReadKeyStroke interfaces.
 *
 * For BIOS, we have a bad PS/2 driver, which should be fine if
 * you're booting with BIOS?
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include "kbd.h"
#include "util.h"
#include "text.h"

#ifdef EFI_PLATFORM
#include <efi.h>
extern EFI_SYSTEM_TABLE *ST;

#define KBD_SCAN_DOWN  0x50
#define KBD_SCAN_UP    0x48
#define KBD_SCAN_LEFT  0x4B
#define KBD_SCAN_RIGHT 0x4D
#define KBD_SCAN_ENTER 0x1C
#define KBD_SCAN_1     2
#define KBD_SCAN_9     10

int read_scancode(int timeout) {
	EFI_INPUT_KEY Key;
	unsigned long int index;
	if (timeout) {
		EFI_EVENT events[] = {ST->ConIn->WaitForKey, 0};
		uefi_call_wrapper(ST->BootServices->CreateEvent, 5, EVT_TIMER, 0, NULL, NULL, &events[1]);
		uefi_call_wrapper(ST->BootServices->SetTimer, 3, events[1], TimerRelative, 10000000UL);
		uefi_call_wrapper(ST->BootServices->WaitForEvent, 3, 2, events, &index);
	} else {
		uefi_call_wrapper(ST->BootServices->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
	}
	EFI_STATUS result = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);

	if (result == EFI_NOT_READY) return -1;
	switch (Key.ScanCode) {
		case 0:
			switch (Key.UnicodeChar) {
				case L'\r':
					return KBD_SCAN_ENTER;
				case L'1':
				case L'2':
				case L'3':
				case L'4':
				case L'5':
				case L'6':
				case L'7':
				case L'8':
				case L'9':
					return Key.UnicodeChar - L'1' + KBD_SCAN_1;
				case L'e':
					return 0x12;
				default:
					return 0xFF;
			}
			break;
		case 0x01: return KBD_SCAN_UP;
		case 0x02: return KBD_SCAN_DOWN;
		case 0x03: return KBD_SCAN_RIGHT;
		case 0x04: return KBD_SCAN_LEFT;
		default:
			return 0xFF;
	}
}

int read_key(int * c) {
	EFI_INPUT_KEY Key;
	unsigned long int index;
	uefi_call_wrapper(ST->BootServices->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
	uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);

	if (Key.ScanCode == 0) {
		*c = Key.UnicodeChar;
		if (*c == '\r') *c = '\n';
		return 0;
	}

	switch (Key.ScanCode) {
		case 0x03: return 3;
		case 0x04: return 2;
		case 0x09: return 4;
		case 0x0a: return 5;
		case 0x17: *c = 27; return 0;
	}

	return 1;
}

#else

int read_cmos_seconds(void) {
	outportb(0x70,0);
	return inportb(0x71);
}

static char kbd_us[128] = {
	0, 27, '1','2','3','4','5','6','7','8','9','0',
	'-','=','\b', '\t', 'q','w','e','r','t','y','u','i','o','p','[',']','\n',
	0, 'a','s','d','f','g','h','j','k','l',';','\'', '`',
	0, '\\','z','x','c','v','b','n','m',',','.','/',
	0, '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static char kbd_us_l2[128] = {
	0, 27, '!','@','#','$','%','^','&','*','(',')',
	'_','+','\b', '\t', 'Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
	0, 'A','S','D','F','G','H','J','K','L',':','"', '~',
	0, '|','Z','X','C','V','B','N','M','<','>','?',
	0, '*', 0, '\037', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	'-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

extern int do_bios_call(unsigned int function, unsigned int arg1);

int read_key(int * c) {
	static int shift_state = 0;

	int sc = read_scancode(0);
	int shift  = do_bios_call(4,2) & 0x3;

	if (sc == 0x4B) return shift ? 4 : 2;
	if (sc == 0x4D) return shift ? 5 : 3;

	if (!(sc & 0x80)) {
		*c = shift ? kbd_us_l2[sc] : kbd_us[sc];
		return *c == 0;
	}

	return 1;
}

int kbd_status(void) {
	int result = do_bios_call(4,0x11);
	return (result & 0xFF) == 0;
}

int read_scancode(int timeout) {
	if (timeout) {
		int start_s = read_cmos_seconds();
		while (kbd_status()) {
			int now_s = read_cmos_seconds();
			if (now_s != start_s) return -1;
		}
	}
	int result = do_bios_call(4,0);
	return (result >> 8) & 0xFF;
}
#endif
