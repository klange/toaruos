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

