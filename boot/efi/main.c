#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "base.h"
#include "text.h"
#include "util.h"
#include "elf.h"
#include "multiboot.h"
#include "kbd.h"
#include "options.h"

#include <kuroko/kuroko.h>
#include <kuroko/vm.h>

EFI_HANDLE ImageHandleIn;

extern void krk_printResult(unsigned long long val);
extern int krk_repl(void);

EFI_STATUS
	EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	/*
	 * Initialize GNU-EFI
	 */
	InitializeLib(ImageHandle, SystemTable);
	ST = SystemTable;
	ImageHandleIn = ImageHandle;

	/*
	 * Disable watchdog
	 */
	uefi_call_wrapper(ST->BootServices->SetWatchdogTimer, 4, 0, 0, 0, NULL);

	/*
	 * Start shell
	 */
	set_attr(0xF);
	krk_initVM(0);
	krk_startModule("__main__");
	krk_interpret(
		"if True:\n"
		" import kuroko\n"
		" print(f'Kuroko {kuroko.version} ({kuroko.builddate}) with {kuroko.buildenv}')\n"
		" kuroko.module_paths = ['/krk/']\n", "<stdin>");
	puts("Type `license` for copyright, `exit()` to return to menu.");

	krk_repl();

	return 0;
}
