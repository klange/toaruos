#include <stdio.h>
#include <setjmp.h>

asm (
	".globl setjmp\n"
	"setjmp:\n"
	"mov x2, sp\n"
	"str x2,  [x0]\n"
	"stp x19, x20, [x0, (2 * 16)]\n"
	"stp x21, x22, [x0, (3 * 16)]\n"
	"stp x23, x24, [x0, (4 * 16)]\n"
	"stp x25, x26, [x0, (5 * 16)]\n"
	"stp x27, x28, [x0, (6 * 16)]\n"
	"stp x29, x30, [x0, (1 * 16)]\n"
	"mov x0, 0\n"
	"ret\n"
	".globl longjmp\n"
	"longjmp:\n"
	"ldr x2, [x0]\n"
	"ldp x19, x20, [x0, (2 * 16)]\n"
	"ldp x21, x22, [x0, (3 * 16)]\n"
	"ldp x23, x24, [x0, (4 * 16)]\n"
	"ldp x25, x26, [x0, (5 * 16)]\n"
	"ldp x27, x28, [x0, (6 * 16)]\n"
	"ldp x29, x30, [x0, (1 * 16)]\n"
	"mov sp, x2\n"
	"mov x0, x1\n"
	"ret\n"
);

#if 0
int _setjmp(jmp_buf env);
void _longjmp(jmp_buf env, int val);

int setjmp(jmp_buf env) {
	fprintf(stderr, "setjmp called\n");
	return _setjmp(env);
}
void longjmp(jmp_buf env, int val) {
	fprintf(stderr, "longjmp called\n");
	_longjmp(env,val);
}
#endif
