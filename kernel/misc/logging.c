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
	blog("Installing stored logging...");
	log_buffer = list_create();
	LOG(INFO, "Kernel log initialized");
	bfinish(0);
}

void debug_print_log_entry(log_entry_t * l) {
	int i;
	i = kprintf("[%s] %s ",
			messages[l->type],
			l->module);
	while (i < 40) {
		kprintf(" ");
		++i;
	}
	i = kprintf("line %d", l->line);
	while (i < 10) {
		kprintf(" ");
		++i;
	}
	kprintf("%s\n", l->text);
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

/*
 * Messsage... <---  ---> [  OK  ]
 */

static char * boot_messages[] = {
	"\033[1;32m  OK  ",
	"\033[1;33m WARN ",
	"\033[1;31mERROR!"
};

char * last_message = NULL;

void blog(char * string) {
	last_message = string;
	kprintf("\033[0m%s\033[1000C\033[8D[ \033[1;34m....\033[0m ]", string);
}
void bfinish(int status) {
	if (!last_message) { return; }
	kprintf("\033[1000D\033[0m%s\033[1000C\033[8D[%s\033[0m]\n", last_message, boot_messages[status]);
}
