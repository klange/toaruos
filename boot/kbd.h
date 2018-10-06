#pragma once

#define KBD_SCAN_DOWN  0x50
#define KBD_SCAN_UP    0x48
#define KBD_SCAN_LEFT  0x4B
#define KBD_SCAN_RIGHT 0x4D
#define KBD_SCAN_ENTER 0x1C
#define KBD_SCAN_1     2
#define KBD_SCAN_9     10

#ifdef EFI_PLATFORM

static int read_scancode(void) {
	EFI_INPUT_KEY Key;
	unsigned long int index;
	uefi_call_wrapper(ST->BootServices->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
	uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
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
				case L'y':
					return 'y';
				case L'n':
					return 'n';
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
#else
static int read_scancode(void) {
	while (!(inportb(0x64) & 1));
	int out;
	while (inportb(0x64) & 1) {
		out = inportb(0x60);
	}
	return out;
}
#endif
