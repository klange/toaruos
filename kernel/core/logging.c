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

static list_t * log_buffer;

static char * messages[] = {
	"info",
	"note",
	"warn",
	"err ",
	"crit"
};

void logging_install() {
	log_buffer = list_create();
	LOG(INFO, "Kernel log initialized");
}

void debug_print_log_entry(log_entry_t * l) {
	kprintf("[%s] %s line %d: %s\n",
			messages[l->type],
			l->module,
			l->line,
			l->text);
}

void debug_print_log() {
	foreach(entry, log_buffer) {
		debug_print_log_entry((log_entry_t *)entry->value);
	}
}

void klog(log_type_t type, char *module, unsigned int line, const char *fmt, ...) {
	if (!log_buffer) return;
	log_entry_t * l = malloc(sizeof(log_entry_t));
	l->type   = type;
	l->module = module;
	l->line   = line;
	l->text   = malloc(sizeof(char) * 1024);
	va_list args;
	va_start(args, fmt);
	vasprintf(l->text, fmt, args);
	va_end(args);
	list_insert(log_buffer, l);
}
