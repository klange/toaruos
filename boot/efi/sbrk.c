#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include <stdio.h>
#include "types.h"

#define MAX_PAGES 16000

static char * base = NULL;
static char * endp = NULL;
static char * curr = NULL;

void * sbrk(size_t bytes) {
	if (!base) {
		EFI_PHYSICAL_ADDRESS allocSpace;
		uefi_call_wrapper(ST->BootServices->AllocatePages, 4, AllocateAnyPages, EfiLoaderData, MAX_PAGES, &allocSpace);
		base = (char *)(intptr_t)allocSpace;
		endp = base + MAX_PAGES * 0x1000;
		curr = base;
	}

	if (curr + bytes >= endp) {
		printf("Error: Ran out of pages.\n");
		return NULL;
	}

	void * out = curr;
	memset(out, 0x00, bytes);
	curr += bytes;
	return out;
}

