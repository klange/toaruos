#include <efi.h>
#include <efilib.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern EFI_HANDLE ImageHandleIn;

static EFI_GUID efi_simple_file_system_protocol_guid =
	{0x0964e5b22,0x6459,0x11d2,0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b};

static EFI_GUID efi_loaded_image_protocol_guid =
	{0x5B1B31A1,0x9562,0x11d2, {0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B}};

static EFI_FILE *root;

static int _initialized = 0;
static void initialize(void) {
	UINTN count;
	EFI_HANDLE * handles;
	EFI_FILE_IO_INTERFACE *efi_simple_filesystem;
	EFI_LOADED_IMAGE * loaded_image;
	EFI_STATUS status;

	status = uefi_call_wrapper(ST->BootServices->HandleProtocol,
			3, ImageHandleIn, &efi_loaded_image_protocol_guid,
			(void **)&loaded_image);

	status = uefi_call_wrapper(ST->BootServices->HandleProtocol,
			3, loaded_image->DeviceHandle, &efi_simple_file_system_protocol_guid,
			(void **)&efi_simple_filesystem);

	status = uefi_call_wrapper(efi_simple_filesystem->OpenVolume,
			2, efi_simple_filesystem, &root);

	_initialized = 1;
}

FILE * fopen(const char * pathname, const char * mode) {
	if (strcmp(mode,"r")) {
		fprintf(stderr, "fopen: unsupported mode '%s'\n", mode);
		return NULL;
	}

	if (!_initialized) initialize();

	uint16_t * tmp = malloc(strlen(pathname) * 2 + 2);

	for (size_t i = 0; pathname[i]; ++i) {
		tmp[i] = pathname[i] == '/' ? '\\' : pathname[i];
		tmp[i+1] = 0;
	}

	EFI_FILE * outPtr;
	EFI_STATUS status = uefi_call_wrapper(root->Open, 5, root, &outPtr, tmp, EFI_FILE_MODE_READ, 0);
	free(tmp);

	if (EFI_ERROR(status)) {
		errno = status & 0xFF;
		return NULL;
	}

	return (FILE *)outPtr;
}

int fclose(FILE * stream) {
	EFI_FILE * s = (EFI_FILE *)stream;

	EFI_STATUS status = uefi_call_wrapper(s->Close, 1, s);
	return status;
}


int fgetc(FILE * stream) {
	EFI_FILE * s = (EFI_FILE *)stream;
	UINTN dataSize = 1;
	unsigned char data[1];

	EFI_STATUS status = uefi_call_wrapper(s->Read, 3, s, &dataSize, &data);
	if (EFI_ERROR(status)) {
		errno = status & 0xFF;
		return -1;
	}

	if (dataSize == 0) {
		s->Revision = 0x1234;
		return -1;
	}
	return data[0];
}

int fseek(FILE * stream, long offset, int whence) {
	EFI_FILE * s = (EFI_FILE *)stream;
	UINT64 realOffset = offset;

	if (whence == SEEK_END) {
		realOffset = 0xFFFFFFFFFFFFFFFFUL;
	}

	EFI_STATUS status = uefi_call_wrapper(s->SetPosition, 2, s, realOffset);
	if (EFI_ERROR(status)) return -1;
	return 0;
}

long ftell(FILE * stream) {
	EFI_FILE * s = (EFI_FILE *)stream;
	UINT64 position;

	EFI_STATUS status = uefi_call_wrapper(s->GetPosition, 2, s, &position);
	if (EFI_ERROR(status)) return -1;

	return position;
}

size_t fread(void * ptr, size_t size, size_t nmemb, FILE * stream) {
	EFI_FILE * s = (EFI_FILE *)stream;
	if (s->Revision == 0x1234) return 0;

	UINTN bufferSize = size * nmemb;

	EFI_STATUS status = uefi_call_wrapper(s->Read, 3, s, &bufferSize, ptr);

	if (bufferSize == 0 && (size * nmemb != 0)) {
		s->Revision = 0x1234;
		return 0;
	}

	return bufferSize / size;
}

int feof(FILE * stream) {
	EFI_FILE * s = (EFI_FILE *)stream;
	if (s->Revision == 0x1234) return 1;
	return 0;
}

int stat(const char* fn,struct stat* outbuf) {
	FILE * f = fopen(fn,"r");
	if (!f) return -1;
	fclose(f);
	return 0;
}

int errno = 0;

char * strerror(int errnum) {
	if (errnum == 14) return "File not found";
	return "unknown";
}
