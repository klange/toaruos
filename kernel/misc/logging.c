/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Kernel Logging Facility
 *
 * Maintains a log in-memory as well as to serial (unless
 * told not to).
 */

#include <system.h>
#include <list.h>
#include <logging.h>
#include <va_list.h>

log_type_t debug_level = NOTICE;

static char * c_messages[] = {
	"\033[1;34mINFO",
	"\033[1;35mNOTICE",
	"\033[1;33mWARNING",
	"\033[1;31mERROR",
	"\033[1;37;41mCRITICAL"
};


void _debug_print(char * title, int line_no, log_type_t level, char *fmt, ...) {
	if (level >= debug_level) {
		va_list args;
		va_start(args, fmt);
		char buffer[1024];
		vasprintf(buffer, fmt, args);
		va_end(args);

		kprintf("[%10d.%2d:%s:%d] %s\033[0m: %s\n", timer_ticks, timer_subticks, title, line_no, c_messages[level], buffer);

	}
	/* else ignore */
}

