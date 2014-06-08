/* This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2011-2014 Kevin Lange
 */
/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Interrupt Services Requests
 */
#include <system.h>
#include <logging.h>

extern irq_handler_t isrs_routines[256];
extern void fault_error(struct regs *r);

void fault_handler(struct regs *r) {
	irq_handler_t handler;
	handler = isrs_routines[r->int_no];
	if (handler) {
		handler(r);
	} else {
		fault_error(r);
	}
}
