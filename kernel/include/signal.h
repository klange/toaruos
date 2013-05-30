/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#ifndef SIGNAL_H
#define SIGNAL_H

#include <types.h>
void return_from_signal_handler();
void fix_signal_stacks();

#include <signal_defs.h>

#endif
