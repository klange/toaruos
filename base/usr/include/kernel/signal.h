/* vim: tabstop=4 shiftwidth=4 noexpandtab
 */

#pragma once

#include <kernel/types.h>
void return_from_signal_handler(void);
void fix_signal_stacks(void);

#include <sys/signal_defs.h>

