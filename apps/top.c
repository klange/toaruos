/**
 * @brief Show processes sorted by resource usage.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021-2022 K. Lange
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <pwd.h>
#include <termios.h>
#include <poll.h>
#include <time.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/signal.h>

#include <sys/sysfunc.h>
#include <toaru/list.h>
#include <toaru/hashmap.h>
#include <toaru/procfs.h>

enum header_columns {
	COLUMN_NONE,
	COLUMN_PID,
	COLUMN_TID,
	COLUMN_USER,
	COLUMN_VSZ,
	COLUMN_SHM,
	COLUMN_MEM,
	COLUMN_CPUA,
	COLUMN_CPU,
	COLUMN_S
};

#define T_T "\033[94m"
#define T_C "\033[0;1m"
#define T_B "\033[1m"
#define T_E "\033[0m"
#define T_K "\033[K"
#define T_S "\033[97m"
#define T_R "\033[30m"
#define T_H "\033[44;30m"
#define T_CR "\033[H"
#define T_FC "\033[0;9%dm"
#define T_BR "\033[90m"

static hashmap_t * process_ents = NULL;
static list_t * ents_list = NULL;
static int cpu_count = 1;
static int sort_column = COLUMN_CPU;
static int show_help = 0;
static int combine_threads = 1;

static const char * help_text[] = {
	"q: quit",
	"w: switch sort column",
	"h: show this help text",
};

enum {
	FORMATTER_DECIMAL,
	FORMATTER_PERCENT,
	FORMATTER_STRING
};

enum {
	SORT_ASC,
	SORT_DEC
};

struct columns {
	const char * title;
	intptr_t     member;
	int          formatter;
	int          width;
	int          sort_order;
} ColumnDescriptions[] = {
	[COLUMN_NONE] = {"", 0, 0, 0, 0},
	[COLUMN_PID]  = {"PID",  offsetof(struct process, tgid),  FORMATTER_DECIMAL, 0, SORT_ASC},
	[COLUMN_TID]  = {"TID",  offsetof(struct process, pid),   FORMATTER_DECIMAL, 0, SORT_ASC},
	[COLUMN_VSZ]  = {"VSZ",  offsetof(struct process, vsz),   FORMATTER_DECIMAL, 0, SORT_DEC},
	[COLUMN_SHM]  = {"SHM",  offsetof(struct process, shm),   FORMATTER_DECIMAL, 0, SORT_DEC},
	[COLUMN_MEM]  = {"%MEM", offsetof(struct process, mem),   FORMATTER_PERCENT, 0, SORT_DEC},
	[COLUMN_CPU]  = {"%CPU", offsetof(struct process, cpu),   FORMATTER_PERCENT, 0, SORT_DEC},
	[COLUMN_CPUA] = {"CPUA", offsetof(struct process, user_data), FORMATTER_PERCENT, 0, SORT_DEC},
	[COLUMN_USER] = {"USER", offsetof(struct process, user_pdata), FORMATTER_STRING,    0, SORT_ASC},
	[COLUMN_S]    = {"S",    offsetof(struct process, state),FORMATTER_STRING,  0, SORT_ASC},
};

static int columns_default[] = { COLUMN_PID, COLUMN_USER, COLUMN_VSZ, COLUMN_SHM, COLUMN_S, COLUMN_CPU, COLUMN_CPUA, COLUMN_MEM, COLUMN_NONE };
static int columns_threads[] = { COLUMN_PID, COLUMN_TID, COLUMN_USER, COLUMN_VSZ, COLUMN_SHM, COLUMN_S, COLUMN_CPU, COLUMN_CPUA, COLUMN_MEM, COLUMN_NONE };
static int * columns = NULL;

/**
 * @brief Print a single column to stdout with the appropriate formatter.
 */
static int print_column(struct process * proc, int column_id) {
	struct columns * column = &ColumnDescriptions[column_id];
	switch (column->formatter) {
		case FORMATTER_DECIMAL: {
			int value = *(int*)((char *)proc + column->member);
			return printf("%*d ", column->width, value);
		}
		case FORMATTER_PERCENT: {
			int value = *(int*)((char *)proc + column->member);
			if (value >= 1000) {
				return printf("%*d ", column->width, value / 10);
			} else {
				return printf("%*d.%01d ", column->width - 2, value / 10, value % 10);
			}
		}
		case FORMATTER_STRING: {
			char * value = *(char**)((char *)proc + column->member);
			return printf("%-*s ", column->width, value);
		}
		default: return 0;
	}
}

/**
 * @brief Calculate the size of a formatted column.
 */
static int size_column(struct process * proc, int column_id) {
	char garbage[100];
	struct columns * column = &ColumnDescriptions[column_id];
	switch (column->formatter) {
		case FORMATTER_DECIMAL: {
			int value = *(int*)((char *)proc + column->member);
			return snprintf(garbage, 100, "%d", value);
		}
		case FORMATTER_PERCENT: {
			int value = *(int*)((char *)proc + column->member);
			if (value >= 1000) {
				return 3;
			} else {
				return snprintf(garbage, 100, "%d.%01d", value / 10, value % 10);
			}
		}
		case FORMATTER_STRING: {
			char * value = *(char**)((char *)proc + column->member);
			return strlen(value);
		}
		default: return 0;
	}
}

/**
 * @brief Print the column headings.
 */
void print_header(void) {
	printf(T_H);
	for (int * c = columns; *c; ++c) {
		if (*c == sort_column) printf(T_S);
		printf("%*s ", ColumnDescriptions[*c].width, ColumnDescriptions[*c].title);
		if (*c == sort_column) printf(T_R);
	}
	if (sort_column == COLUMN_NONE) {
		printf(T_B T_S "CMD" T_R);
	} else {
		printf("CMD");
	}
	printf(T_K T_E "\n");
}

/**
 * @brief Reset column widths to the minimum required to fit their headings.
 */
void reset_column_widths(void) {
	for (size_t i = 0; i < sizeof(ColumnDescriptions) / sizeof(*ColumnDescriptions); ++i) {
		ColumnDescriptions[i].width = strlen(ColumnDescriptions[i].title);
	}
}

/**
 * @brief Print one entry to stdout with the appropriate formatter.
 *
 * @p out Process entry to print.
 * @p width Total available screen width.
 */
void print_entry(struct process * out, int width) {
	int used = 0;
	for (int * c = columns; *c; ++c) {
		if (*c == sort_column) printf(T_C);
		used += print_column(out, *c);
		if (*c == sort_column) printf(T_E);
	}
	const char * color = "";
	if (out->pid != out->tgid) color = T_T;

	printf("%s%.*s" T_E T_K "\n", color, width - used, out->cmdline ? out->cmdline: out->name);
}

/**
 * @brief Given a process, expand any columns that need to be bigger to fit it.
 */
void update_column_widths(struct process * out) {
	int len;
	for (int * c = columns; *c; ++c) {
		if ((len = size_column(out, *c)) > ColumnDescriptions[*c].width) ColumnDescriptions[*c].width = len;
	}
}

/**
 * @brief Free resources used by a process entry.
 *
 * Frees any strings allocated for the process, as well
 * as the process struct itself.
 */
void free_entry(struct process * out) {
	if (out->user_pdata) free(out->user_pdata);
	procfs_free(out);
}

/**
 * @brief Given a UID, get the username.
 *
 * Always returns a string that must be freed. If the uid
 * could not be found in the passwd database, the uid itself
 * is formatted as a string for display.
 */
char * format_username(int uid) {
	static char *tmp;
	struct passwd * p = getpwuid(uid);
	if (p) {
		asprintf(&tmp, "%-8s", p->pw_name);
	} else {
		asprintf(&tmp, "%-8d", uid);
	}
	endpwent();
	return tmp;
}

/**
 * @brief Find a process from its pid.
 *
 * Used for looking up the main thread's process entry when
 * collecting information for non-main threads, so we can
 * sum up CPU usage, which is reported in procfs per-thread.
 */
struct process * process_from_pid(pid_t pid) {
	return hashmap_get(process_ents, (void*)(uintptr_t)pid);
}


int top_callback(struct process * out, void * ctx) {
	if (out->tgid != out->pid && combine_threads) {
		struct process * parent = process_from_pid(out->tgid);
		if (parent) {
			int t = 0;
			for (int i = 0; i < 4; ++i) {
				parent->cpu[i] += out->cpu[i];
				t += out->cpu[i];
			}
			parent->user_data = t / 4;
		}
		return 0;
	}

	hashmap_set(process_ents, (void*)(uintptr_t)out->pid, out);
	list_insert(ents_list, (void *)out);

	out->user_data = 0;
	for (int i = 0; i < 4; ++i) {
		out->user_data += out->cpu[i];
	}
	out->user_data /= 4;

	out->user_pdata = format_username(out->uid);

	if (out->cmdline) {
		/* Replace \x1e with spaces */
		for (size_t i = 0; i < out->cmdline_len; ++i) {
			if (out->cmdline[i] == 30) out->cmdline[i] = ' ';
		}
	}

	update_column_widths(out);
	return 0;
}

/**
 * @brief Sort an array of process struct pointers using the
 *        currently selected sort column.
 */
static int sort_processes(const void * a, const void * b) {
	struct process * left  = *(struct process **)a;
	struct process * right = *(struct process **)b;

	struct columns * column = &ColumnDescriptions[sort_column];

	if (sort_column == COLUMN_NONE) {
		return strcmp(left->cmdline, right->cmdline);
	}

	switch (column->formatter) {
		case FORMATTER_DECIMAL:
		case FORMATTER_PERCENT: {
			int a = *(int*)((char *)left + column->member);
			int b = *(int*)((char *)right + column->member);
			return (column->sort_order == SORT_ASC) ? (a - b) : (b - a);
		}
		case FORMATTER_STRING: {
			char * a = *(char **)((char *)left + column->member);
			char * b = *(char **)((char *)right + column->member);
			return (column->sort_order == SORT_ASC) ? strcmp(a,b) : strcmp(b,a);
		}
		default: return 0;
	}
}

/**
 * @brief Collect memory usage information from /proc/meminfo
 *
 * @p total (out) Total memory available in KiB
 * @p used  (out) In-use memory in KiB
 */
static void get_mem_info(int * total, int * used) {
	FILE * f = fopen("/proc/meminfo", "r");
	if (!f) return;
	int free;
	char buf[1024] = {0};
	fgets(buf, 1024, f);

	char * a, * b;
	a = strchr(buf, ' ');
	a++;
	b = strchr(a, '\n');
	*b = '\0';
	*total = atoi(a);
	fgets(buf, 1024, f);
	a = strchr(buf, ' ');
	a++;
	b = strchr(a, '\n');
	*b = '\0';
	free = atoi(a);
	*used = *total - free;

	fclose(f);
}

/**
 * @brief Collect CPU usage information from /proc/idle
 *
 * @p cpus (out) Array of CPU usage in permilles.
 */
static void get_cpu_info(int cpus[]) {
	FILE * f = fopen("/proc/idle","r");
	char buf[4096];
	fread(buf, 4096, 1, f);

	char * buffer = buf;
	for (int i = 0; i < cpu_count; ++i) {
		char * b = strchr(buffer, ':');
		b++;
		cpus[i] = 1000 - atoi(b);
		if (cpus[i] < 0) cpus[i] = 0;
		buffer = strchr(b, '\n');
	}

	fclose(f);
}

/**
 * @brief Obtain information on how much system memory is
 *        being used for tmpfs blocks.
 */
static void get_tmpfs_info(size_t * size) {
	FILE * f = fopen("/proc/tmpfs", "r");
	if (!f) return;

	char buf[1024] = {0};
	fread(buf,1,1024,f);
	fclose(f);

	/* Should probably be looking for UsedBlocks: and advancing from there... */
	char *b = strstr(buf, ":");
	if (!b) return;
	b += 2;

	*size = strtoul(b,NULL,10) * 4; /* Expressed in pages, so * 4 for kilobytes */
}

static int fill_colors[] = {
	1, 3, 4, 5, 6
};

/**
 * @brief Display a progress-bar-style usage meter.
 *
 * @p title Label to apply to the meter, shown on left.
 * @p label Label to show inside of the meter, show on the right.
 * @p width Available width to display the meter in, including title and frame.
 * @p count Number of values to display.
 * @p filled Values to stack in the meter.
 * @p maximum Maximum value of the meter.
 */
static void print_meter(const char * title, const char * label, int width, int count, int filled[], int maximum) {
	int available = width - strlen(title) - 4;
	int remaining = available;
	int fillSlots = 0;

	/* Count total fill slots */
	for (int i = 0; i < count; ++i) {
		filled[i] = filled[i] * available / maximum;
		if (filled[i] < 0) filled[i] = 0;
		if (filled[i] > remaining) filled[i] = remaining;
		fillSlots += filled[i];
		remaining = available - fillSlots;
	}

	int emptSlots = available - fillSlots;

	printf(T_B "%s [", title);

	char * fill = malloc(available + 1);
	size_t j = 0;
	for (int i = 0; i < fillSlots; ++i, j++) fill[j] = '|';
	for (int i = 0; i < emptSlots; ++i, j++) fill[j] = ' ';

	size_t l = strlen(label);
	if (available > (int)l) {
		sprintf(fill + available - l, "%s", label);
	}

	j = 0;

	for (int c = 0; c < count; ++c) {
		printf(T_FC, fill_colors[c % (sizeof(fill_colors) / sizeof(int))]);
		for (int i = 0; i < filled[c]; ++i, j++) printf("%c",fill[j]);
	}
	printf(T_BR);
	for (int i = 0; i < emptSlots; ++i, j++) printf("%c",fill[j]);

	printf(T_C "]" T_E " ");
	free(fill);
}

/**
 * @brief Switch sorting to the next column.
 */
static void next_sort_order(void) {
	size_t column_count = 1; /* NONE is always in there */
	for (int * i = columns; *i; ++i) column_count++;

	for (size_t i = 0; i < column_count; ++i) {
		if (columns[i] == sort_column) {
			sort_column = columns[(i + 1) % column_count];
			return;
		}
	}
}

/**
 * @brief Switch sorting to the previous column.
 */
static void prev_sort_order(void) {
	size_t column_count = 1; /* NONE is always in there */
	for (int * i = columns; *i; ++i) column_count++;

	for (size_t i = 0; i < column_count; ++i) {
		if (columns[i] == sort_column) {
			sort_column = columns[(i + column_count - 1) % column_count];
			return;
		}
	}
}

static void toggle_threads(void) {
	if (combine_threads) {
		combine_threads = 0;
		columns = columns_threads;
	} else {
		combine_threads = 1;
		columns = columns_default;
		if (sort_column == COLUMN_TID) sort_column = COLUMN_PID;
	}
}

/**
 * @brief Collect information on running processes.
 */
static struct process ** read_processes(size_t * count) {
	/* Set minimum column widths to titles */
	reset_column_widths();

	/* Read the entries in the directory */
	ents_list = list_create();
	process_ents = hashmap_create_int(10);

	procfs_iterate(top_callback, NULL, PROCFSLIB_NO_FREE | PROCFSLIB_COLLECT_COMMANDLINE);

	hashmap_free(process_ents);
	free(process_ents);

	/* Turn list into an array */
	*count = ents_list->length;
	struct process ** processList = malloc(sizeof(struct process*) * *count);
	size_t ent = 0;
	while (ents_list->length) {
		node_t * node = list_pop(ents_list);
		processList[ent] = node->value;
		free(node);
		ent++;
	}
	free(ents_list);

	/* Sort processes with the current sort column */
	qsort(processList, *count, sizeof(struct process*), sort_processes);

	return processList;
}

/**
 * @brief Gather system information and print one sample.
 */
static int do_once(void) {
	size_t count;
	struct process ** processList = read_processes(&count);

	/* Gather total memory usage /proc/meminfo */
	int mem_total = 0, mem_used = 0;
	get_mem_info(&mem_total, &mem_used);

	size_t mem_tmpfs = 0;
	get_tmpfs_info(&mem_tmpfs);

	/* Gather per-CPU usage from /proc/idle */
	int cpus[32];
	get_cpu_info(cpus);

	/* Gather screen size */
	struct winsize w;
	ioctl(STDOUT_FILENO, TIOCGWINSZ, &w);

	/* Figure out how we're going to lay out widgets */
	int top_rows = 1 + cpu_count;
	int meter_width = w.ws_col / 2;
	int current_row = 0;
	int left_side = 1;

	/* Generate info rows */
	int info_width = w.ws_col - meter_width;
	char info_row[5][100] = {0};
	int info_rows = 0;

	if (info_width <= 30) {
		meter_width = w.ws_col;
		info_width = 0;
	} else {
		if (top_rows >= 1) {
			info_rows = 1;
			char tmp[256] = {0};
			gethostname(tmp, 255);
			snprintf(info_row[0], 99, T_T "Hostname: " T_C "%.*s" T_E, info_width - 10, tmp);
		}
		if (top_rows >= 2) {
			info_rows = 2;
			char tmp[256] = {0};
			char * format = "%a %b %d %T %Y %Z";
			struct tm * timeinfo;
			struct timeval now;
			gettimeofday(&now, NULL);
			timeinfo = localtime((time_t *)&now.tv_sec);
			strftime(tmp,255,format,timeinfo);
			snprintf(info_row[1], 99, T_T "Time: " T_C "%.*s" T_E, info_width - 6, tmp);
		}
		if (top_rows >= 3) {
			info_rows = 3;
			snprintf(info_row[2], 99, T_T "%s: " T_C "%lu" T_E, combine_threads ? "Tasks" : "Threads", count);
		}
	}

	/* Reset cursor to upper left */
	printf(T_CR);

	/* Display CPU usage widgets */
	for (int cpu = 0; cpu < cpu_count; ++cpu) {
		char name[20], usage[30];
		sprintf(name, "%3d", cpu + 1);
		sprintf(usage, "%d.%01d%%", cpus[cpu] / 10, cpus[cpu] % 10);
		print_meter(name, usage, left_side ? meter_width : info_width, 1, (int[]){cpus[cpu]}, 1000);

		if (current_row < info_rows) {
			printf("%s" T_K "\n", info_row[current_row]);
			current_row++;
		} else if (info_rows) {
			if (left_side) {
				left_side = 0;
			} else {
				left_side = 1;
				current_row++;
			}
		} else {
			current_row++;
		}
	}

	/* Display memory usage widget */
	char memUsed[30];
	sprintf(memUsed, "%dM/%dM", mem_used / 1024, mem_total / 1024);
	print_meter("Mem", memUsed, left_side ? meter_width : info_width, 2, (int[]){mem_used-mem_tmpfs,mem_tmpfs}, mem_total);
	if (left_side && current_row < info_rows) {
		printf("%s", info_row[current_row]);
	}
	current_row++;
	printf(T_K "\n");

	/* Show column headers */
	print_header();
	int i = 0;
	size_t ent = 0;

	/* Print entries, or help text lines */
	if (show_help) {
		for (ent = 0; ent < sizeof(help_text) / sizeof(*help_text); ++i, ++ent) {
			if (i >= w.ws_row - current_row - 2) break;
			printf("%*s" T_K "\n", (int)w.ws_col, help_text[ent]);
		}
	} else {
		for (ent = 0; ent < count; ++i, ++ent) {
			if (i >= w.ws_row - current_row - 2) break;
			print_entry(processList[ent], w.ws_col);
		}
	}

	/* Clear remaining screen lines */
	for (; i < w.ws_row - current_row - 2; ++i) {
		printf(T_K "\n");
	}

	/* Clean up process data from this round */
	for (ent = 0; ent < count; ++ent) {
		free_entry(processList[ent]);
	}
	free(processList);

	/* Wait for command or 2 seconds for next refresh... */
	struct pollfd fds[1];
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	int ret = poll(fds,1,2000);
	if (ret > 0 && fds[0].revents & POLLIN) {
		int c = fgetc(stdin);
		if (c == 'q') return 0;
		if (c == 'w') next_sort_order();
		if (c == 'W') prev_sort_order();
		if (c == 'h') show_help = !show_help;
		if (c == 'T') toggle_threads();
	}

	return 1;
}

/**
 * @brief Gather and print process information once.
 *
 * Prints only process information.
 */
static int do_log(void) {
	size_t count;
	struct process ** processList = read_processes(&count);

	int mem_total = 0, mem_used = 0;
	get_mem_info(&mem_total, &mem_used);

	size_t mem_tmpfs = 0;
	get_tmpfs_info(&mem_tmpfs);

	/* Gather per-CPU usage from /proc/idle */
	int cpus[32];
	get_cpu_info(cpus);

	/* Hostname */
	{
		char tmp[256] = {0};
		gethostname(tmp, 255);
		printf("Hostname: %s\n", tmp);
	}

	/* Current time */
	{
		char tmp[256] = {0};
		char * format = "%a %b %d %T %Y %Z";
		struct tm * timeinfo;
		struct timeval now;
		gettimeofday(&now, NULL);
		timeinfo = localtime((time_t *)&now.tv_sec);
		strftime(tmp,255,format,timeinfo);
		printf("Time: %s\n", tmp);
	}

	/* Task count and memory usage on one line */
	printf("Tasks: %-7lu Mem: %dM/%dM (%ldM tmpfs)\n", count, mem_used / 1024, mem_total / 1024, mem_tmpfs/1024);

	/* CPU usage; all on one line; formatted best for small counts and <100% usage */
	for (int cpu = 0; cpu < cpu_count; ++cpu) {
		printf("CPU%2d:%3d.%01d%%%s", cpu + 1, cpus[cpu] / 10, cpus[cpu] % 10,
			(cpu + 1 == cpu_count) ? "\n" : "   ");
	}

	/* Blank line to separator process table */
	printf("\n");

	for (int * c = columns; *c; ++c) {
		printf("%*s ", ColumnDescriptions[*c].width, ColumnDescriptions[*c].title);
	}
	printf("CMD\n");

	for (size_t ent = 0; ent < count; ++ent) {
		struct process * out = processList[ent];
		for (int * c = columns; *c; ++c) {
			print_column(out, *c);
		}
		printf("%s\n", out->cmdline ? out->cmdline : out->name);
		free(out);
	}
	free(processList);

	return 0;
}

struct termios old;
void get_initial_termios(void) {
	tcgetattr(STDOUT_FILENO, &old);
}

/**
 * @brief Switch to alt screen, turn on raw input.
 */
void set_unbuffered(void) {
	struct termios new = old;
	new.c_iflag &= (~ICRNL) & (~IXON);
	new.c_lflag &= (~ICANON) & (~ECHO);
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &new);
	printf("\033[?1049h\033[?25l\033[H\033[2J");
}

/**
 * @brief Switch to main screen, re-enable buffering.
 */
void set_buffered(void) {
	printf("\033[H\033[2J\033[?25h\033[?1049l");
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &old);
}

void SIGWINCH_handler(int sig) {
	(void)sig;
	signal(SIGWINCH, SIGWINCH_handler);
}

int main (int argc, char * argv[]) {
	/* Assume CPU count doesn't change... */
	cpu_count = sysfunc(TOARU_SYS_FUNC_NPROC, NULL);
	columns = columns_default;

	/*
	 * If we are writing to a regular file, use the simple log format and
	 * only output one sample of data before exiting.
	 */
	if (!isatty(STDOUT_FILENO)) {
		return do_log();
	}

	/* Initialize terminal for alt screen */
	get_initial_termios();
	set_unbuffered();
	signal(SIGWINCH, SIGWINCH_handler);

	/* Loop */
	while (do_once());

	/* Reset terminal */
	set_buffered();

	return 0;
}
