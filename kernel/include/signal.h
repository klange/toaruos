/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#ifndef SIGNAL_H
#define SIGNAL_H

#include <types.h>
void return_from_signal_handler(void);
void fix_signal_stacks(void);

#include <signal_defs.h>

#endif
