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
