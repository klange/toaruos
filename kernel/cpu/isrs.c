/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Interrupt Services Requests
 */
#include <system.h>
#include <logging.h>

/*
 * XXX XXX XXX XXX
 * This file cointains but one function - and it doesn't work in gcc except when compiled -O2! 
 * It is a deep and dark mystery, and we will work around it with some very terrible macros and
 * attributes and you should probably just ignore it entirely.
 *
 * If you figure out why the output from GCC 4.6.0 (or any other gcc for that matter) fails
 * except when using -O2 (the optimize(2) below) and you are not me, then please do let me know
 * and I will actually send you a cookie (no guarantees on where the cookie came from, but
 * it probably won't be me, I can't bake worth shit.)
 * XXX XXX XXX XXX
 */

irq_handler_t isrs_routines[256];
extern void fault_error(struct regs *r);

void
#ifndef __clang__
/* I'm sorry, I'm going to assume gcc here. */
__attribute__((optimize(2)))
#endif
fault_handler(struct regs *r) {
	irq_handler_t handler;
	handler = isrs_routines[r->int_no];
	if (handler) {
		handler(r);
	} else {
		fault_error(r);
	}
}
