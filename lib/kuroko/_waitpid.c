#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/util.h>

KRK_Function(waitpid) {
	int pid = -1;
	int options = 0;
	if (!krk_parseArgs("|ii",(const char*[]){"pid","options"}, &pid, &options)) return NONE_VAL();

	int status = 0;
	pid_t result = waitpid(pid, &status, options);

	if (result == -1) {
		return krk_runtimeError(KRK_EXC(OSError), "%s", strerror(errno));
	}

	krk_push(OBJECT_VAL(krk_newTuple(2)));
	AS_TUPLE(krk_peek(0))->values.values[0] = INTEGER_VAL(result);
	AS_TUPLE(krk_peek(0))->values.values[1] = INTEGER_VAL(status);
	AS_TUPLE(krk_peek(0))->values.count = 2;
	return krk_pop();
}

KrkValue krk_module_onload__waitpid(void) {
	KrkInstance * module = krk_newInstance(KRK_BASE_CLASS(module));
	krk_push(OBJECT_VAL(module));

	BIND_FUNC(module,waitpid);

#define BIND_CONST(name) krk_attachNamedValue(&module->fields, #name, INTEGER_VAL(name))

	BIND_CONST(WNOHANG);
	BIND_CONST(WUNTRACED);
	BIND_CONST(WSTOPPED);
	BIND_CONST(WNOKERN);

	return krk_pop();
}
