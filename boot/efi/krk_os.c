#include <efi.h>
#include <efilib.h>
#include <stdio.h>
#include <kuroko/vm.h>
#include <kuroko/object.h>
#include <kuroko/util.h>

KRK_FUNC(uname,{
	KrkValue result = krk_dict_of(0, NULL, 0);
	krk_push(result);

	/* UEFI version information */
	krk_attachNamedObject(AS_DICT(result), "sysname",  (KrkObj*)S("UEFI"));

	char tmp[20];
	size_t len = snprintf(tmp,20,"%d.%02d",
		((ST->Hdr.Revision) & 0xFFFF0000) >> 16,
		((ST->Hdr.Revision) & 0x0000FFFF));

	krk_attachNamedObject(AS_DICT(result), "release",  (KrkObj*)krk_copyString(tmp,len));

	/* Firmware information */
	struct StringBuilder sb = {0};
	uint16_t * fw = ST->FirmwareVendor;
	while (*fw) {
		pushStringBuilder(&sb, *fw);
		fw++;
	}
	krk_attachNamedValue(AS_DICT(result), "nodename", finishStringBuilder(&sb));

	len = snprintf(tmp, 20, "%x", ST->FirmwareRevision);
	krk_attachNamedObject(AS_DICT(result), "version",  (KrkObj*)krk_copyString(tmp,len));

#ifdef __x86_64__
	krk_attachNamedObject(AS_DICT(result), "machine",  (KrkObj*)S("x86-64"));
#else
	krk_attachNamedObject(AS_DICT(result), "machine",  (KrkObj*)S("i386"));
#endif

	return krk_pop();
})

extern EFI_HANDLE ImageHandleIn;

KRK_FUNC(exit,{
	uefi_call_wrapper(ST->BootServices->Exit, 4, ImageHandleIn, EFI_SUCCESS, 0, NULL);

})

void _createAndBind_osMod(void) {
	KrkInstance * module = krk_newInstance(vm.baseClasses->moduleClass);
	krk_attachNamedObject(&vm.modules, "os", (KrkObj*)module);
	krk_attachNamedObject(&module->fields, "__name__", (KrkObj*)S("os"));
	krk_attachNamedValue(&module->fields, "__file__", NONE_VAL());
	BIND_FUNC(module,uname);

	BIND_FUNC(vm.builtins,exit);
}


