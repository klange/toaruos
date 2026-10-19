/**
 * @file    fcntl.c
 * @author  K. Lange <klange@toaruos.org>
 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <kuroko/kuroko.h>
#include <kuroko/vm.h>
#include <kuroko/util.h>

KRK_Function(fcntl) {
	int fd;
	int cmd;
	int arg = 0; /* FIXME None of our supported operations take pointers, but lock operations do... */

	if (!krk_parseArgs("ii|i", (const char *[]){"fd","cmd","arg"},
		&fd, &cmd, &arg)) return NONE_VAL();

	int ret = fcntl(fd, cmd, arg);
	if (ret == -1) return krk_runtimeError(KRK_EXC(OSError), "%s", strerror(errno));

	return INTEGER_VAL(ret);
}

KRK_Module(fcntl) {
	BIND_FUNC(module,fcntl);

#define BIND_CONST(name) krk_attachNamedValue(&module->fields, #name, INTEGER_VAL(name))

	BIND_CONST(F_GETFD);
	BIND_CONST(F_SETFD);

	BIND_CONST(F_GETFL);
	BIND_CONST(F_SETFL);

	BIND_CONST(F_DUPFD);

#if defined(F_DUPFD_CLOEXEC)
	BIND_CONST(F_DUPFD_CLOEXEC);
#endif

#if defined(F_DUPFD_CLOFORK)
	BIND_CONST(F_DUPFD_CLOFORK);
#endif

#if defined(F_DUPFD_QUERY)
	BIND_CONST(F_DUPFD_QUERY);
#endif

#if defined(F_CREATED_QUERY)
	BIND_CONST(F_CREATED_QUERY);
#endif

#if defined(FD_CLOEXEC)
	BIND_CONST(FD_CLOEXEC);
#endif

#if defined(FD_CLOFORK)
	BIND_CONST(FD_CLOFORK);
#endif

	/* We don't support these but they're included anyway */
	BIND_CONST(F_GETLK);
	BIND_CONST(F_SETLK);
	BIND_CONST(F_SETLKW);
	BIND_CONST(F_RDLCK);
	BIND_CONST(F_WRLCK);
	BIND_CONST(F_UNLCK);
}

