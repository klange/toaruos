#include <efi.h>
#include <efilib.h>
#include "kbd.h"

int read_scancode(void) {
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

int read_key(void) {
	EFI_INPUT_KEY Key;
	unsigned long int index;
	uefi_call_wrapper(ST->BootServices->WaitForEvent, 3, 1, &ST->ConIn->WaitForKey, &index);
	uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &Key);
	return Key.UnicodeChar;
}
