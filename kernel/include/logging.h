#ifndef LOGGING_H
#define LOGGING_H

typedef enum {
	INFO = 0, /* Unimportant */
	NOTICE,   /* Important, but not bad */
	WARNING,  /* Not what was expected, but still okay */
	ERROR,    /* This is bad... */
	CRITICAL  /* Shit */
} log_type_t;

typedef struct {
	log_type_t   type;
	char *       module;
	unsigned int line;
	char *       text;
} log_entry_t;

void klog(log_type_t type, char *module, unsigned int line, const char *fmt, ...);

#define LOG(type, ...) klog((type), __FILE__, __LINE__, __VA_ARGS__)

void debug_print_log();
void logging_install();

void blog(char * string);
void bfinish(int status);

#endif
