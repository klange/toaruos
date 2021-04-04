#include <stdio.h>
#include <kuroko/vm.h>
#include <kuroko/object.h>
#include <kuroko/util.h>
#include "text.h"

void krk_printResult(KrkValue result) {
	if (IS_NONE(result)) return;
	KrkClass * type = krk_getType(result);
	if (type->_reprer) {
		krk_push(result);
		result = krk_callSimple(OBJECT_VAL(type->_reprer), 1, 0);
		if (IS_STRING(result)) {
			set_attr(0x8);
			printf(" => ");
			puts(AS_CSTRING(result));
			set_attr(0x7);
		}
	}
}

KRK_FUNC(uname,{
	KrkValue result = krk_dict_of(0, NULL, 0);
	krk_push(result);

	krk_attachNamedObject(AS_DICT(result), "sysname",  (KrkObj*)S("?"));
	krk_attachNamedObject(AS_DICT(result), "nodename", (KrkObj*)S("?"));
	krk_attachNamedObject(AS_DICT(result), "release",  (KrkObj*)S("?"));
	krk_attachNamedObject(AS_DICT(result), "version",  (KrkObj*)S("?"));
	krk_attachNamedObject(AS_DICT(result), "machine",  (KrkObj*)S("?"));

	return krk_pop();;
})

void _createAndBind_osMod(void) {
	KrkInstance * module = krk_newInstance(vm.baseClasses->moduleClass);
	krk_attachNamedObject(&vm.modules, "os", (KrkObj*)module);
	krk_attachNamedObject(&module->fields, "__name__", (KrkObj*)S("os"));
	krk_attachNamedValue(&module->fields, "__file__", NONE_VAL());
	BIND_FUNC(module,uname);
}

/* Stubs */
void _createAndBind_timeMod(void) {}
void _createAndBind_fileioMod(void) {}

