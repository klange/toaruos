/**
 * @brief Show processes sorted by resource usage.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
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
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include <sys/sysfunc.h>
#include <toaru/list.h>
#include <toaru/hashmap.h>

#define LINE_LEN 4096

static int show_all = 0;
static int show_threads = 0;
static int show_username = 0;
static int show_mem = 0;
static int show_cpu = 0;
static int collect_commandline = 0;
static int cpu_count = 1;

static int widths[] = {3,3,4,3,3,4,4};

struct process {
	int uid;
	int pid;
	int tid;
	int mem;
	int vsz;
	int shm;
	int cpu;
	char * process;
	char * command_line;
};

static hashmap_t * process_ents = NULL;

void print_username(int uid) {
	struct passwd * p = getpwuid(uid);

	if (p) {
		printf("%-8s", p->pw_name);
	} else {
		printf("%-8d", uid);
	}

	endpwent();
}

struct process * process_from_pid(pid_t pid) {
	return hashmap_get(process_ents, (void*)(uintptr_t)pid);
}

struct process * process_entry(struct dirent *dent) {
	char tmp[300];
	FILE * f;
	char line[LINE_LEN];

	int pid = 0, uid = 0, tgid = 0, mem = 0, shm = 0, vsz = 0, cpu = 0;
	char name[100];

	sprintf(tmp, "/proc/%s/status", dent->d_name);
	f = fopen(tmp, "r");

	if (!f) {
		return NULL;
	}

	line[0] = 0;

	while (fgets(line, LINE_LEN, f) != NULL) {
		char * n = strstr(line,"\n");
		if (n) { *n = '\0'; }
		char * tab = strstr(line,"\t");
		if (tab) {
			*tab = '\0';
			tab++;
		}
		if (strstr(line, "Pid:") == line) {
			pid = atoi(tab);
		} else if (strstr(line, "Uid:") == line) {
			uid = atoi(tab);
		} else if (strstr(line, "Tgid:") == line) {
			tgid = atoi(tab);
		} else if (strstr(line, "Name:") == line) {
			strcpy(name, tab);
		} else if (strstr(line, "VmSize:") == line) {
			vsz = atoi(tab);
		} else if (strstr(line, "RssShmem:") == line) {
			shm = atoi(tab);
		} else if (strstr(line, "MemPermille:") == line) {
			mem = atoi(tab);
		} else if (strstr(line, "CpuPermille:") == line) {
			cpu = atoi(tab);
		}
	}

	fclose(f);

	if (!show_all) {
		/* Filter not ours */
		if (uid != getuid()) return NULL;
	}

	if (!show_threads) {
		if (tgid != pid) {
			/* Add this thread's CPU usage to the parent */
			struct process * parent = process_from_pid(tgid);
			if (parent) {
				parent->cpu += cpu;
			}
			return NULL;
		}
	}

	struct process * out = malloc(sizeof(struct process));
	out->uid = uid;
	out->pid = tgid;
	out->tid = pid;
	out->mem = mem;
	out->shm = shm;
	out->vsz = vsz;
	out->cpu = cpu;
	out->process = strdup(name);
	out->command_line = NULL;

	hashmap_set(process_ents, (void*)(uintptr_t)pid, out);

	char garbage[1024];
	int len;

	if ((len = sprintf(garbage, "%d", out->pid)) > widths[0]) widths[0] = len;
	if ((len = sprintf(garbage, "%d", out->tid)) > widths[1]) widths[1] = len;
	if ((len = sprintf(garbage, "%d", out->vsz)) > widths[3]) widths[3] = len;
	if ((len = sprintf(garbage, "%d", out->shm)) > widths[4]) widths[4] = len;
	if ((len = sprintf(garbage, "%d.%01d", out->mem / 10, out->mem % 10)) > widths[5]) widths[5] = len;
	if ((len = sprintf(garbage, "%d.%01d", out->cpu / 10, out->cpu % 10)) > widths[6]) widths[6] = len;

	struct passwd * p = getpwuid(out->uid);
	if (p) {
		if ((len = strlen(p->pw_name)) > widths[2]) widths[2] = len;
	} else {
		if ((len = sprintf(garbage, "%d", out->uid)) > widths[2]) widths[2] = len;
	}
	endpwent();

	if (collect_commandline) {
		sprintf(tmp, "/proc/%s/cmdline", dent->d_name);
		f = fopen(tmp, "r");
		char foo[1024];
		int s = fread(foo, 1, 1024, f);
		if (s > 0) {
			out->command_line = malloc(s + 1);
			memset(out->command_line, 0, s + 1);
			memcpy(out->command_line, foo, s);

			for (int i = 0; i < s; ++i) {
				if (out->command_line[i] == 30) {
					out->command_line[i] = ' ';
				}
			}

		}
		fclose(f);
	}

	return out;
}

void print_header(void) {
	printf("\033[7m");
	if (show_username) {
		printf("%-*s ", widths[2], "USER");
	}
	printf("%*s ", widths[0], "PID");
	if (show_threads) {
		printf("%*s ", widths[1], "TID");
	}
	if (show_cpu) {
		printf("%*s ", widths[6], "%CPU");
	}
	if (show_mem) {
		printf("%*s ", widths[5], "%MEM");
		printf("%*s ", widths[3], "VSZ");
		printf("%*s ", widths[4], "SHM");
	}
	printf("CMD\033[K\033[0m\n");
}

void print_entry(struct process * out, int width) {
	int used = 0;
	if (show_username) {
		struct passwd * p = getpwuid(out->uid);
		if (p) {
			used += printf("%-*s ", widths[2], p->pw_name);
		} else {
			used += printf("%-*d ", widths[2], out->uid);
		}
		endpwent();
	}
	used += printf("%*d ", widths[0], out->pid);
	if (show_threads) {
		used += printf("%*d ", widths[1], out->tid);
	}
	if (show_cpu) {
		char tmp[10];
		printf("\033[1m");
		sprintf(tmp, "%*d.%01d", widths[6]-2, out->cpu / 10, out->cpu % 10);
		used += printf("%*s ", widths[6], tmp);
		printf("\033[0m");
	}
	if (show_mem) {
		char tmp[10];
		sprintf(tmp, "%*d.%01d", widths[5]-2, out->mem / 10, out->mem % 10);
		used += printf("%*s ", widths[5], tmp);
		used += printf("%*d ", widths[3], out->vsz);
		used += printf("%*d ", widths[4], out->shm);
	}
	int remaining = width - used;
	if (out->command_line) {
		remaining -= printf("%.*s", remaining, out->command_line);
	} else {
		remaining -= printf("%.*s", remaining, out->process);
	}
	printf("\033[K\n");
}

static int sort_processes(const void * a, const void * b) {
	struct process * left  = *(struct process **)a;
	struct process * right = *(struct process **)b;

	return right->cpu - left->cpu;
}

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

static void print_meter(const char * title, const char * label, int width, int filled, int maximum) {
	if (filled < 0) filled = 0;
	if (filled > maximum) filled = maximum;
	int available = width - strlen(title) - 4;
	int fillSlots = filled * available / maximum;
	int emptSlots = available - fillSlots;

	printf("\033[1m%s [\033[0;91m", title);

	char * fill = malloc(available + 1);
	size_t j = 0;
	for (int i = 0; i < fillSlots; ++i, j++) fill[j] = '|';
	for (int i = 0; i < emptSlots; ++i, j++) fill[j] = ' ';

	size_t l = strlen(label);
	if (available > (int)l) {
		sprintf(fill + available - l, "%s", label);
	}

	j = 0;
	for (int i = 0; i < fillSlots; ++i, j++) printf("%c",fill[j]);
	printf("\033[90m");
	for (int i = 0; i < emptSlots; ++i, j++) printf("%c",fill[j]);

	printf("\033[0;1m]\033[0m ");
	free(fill);
}

static void get_cpu_info(int cpus[]) {
	FILE * f = fopen("/proc/smp","r");
	char buf[4096];
	fread(buf, 4096, 1, f);

	char * buffer = buf;
	for (int i = 0; i < cpu_count; ++i) {
		char * a = strchr(buffer, ':');
		a += 2;
		char * b = strchr(a, ' ');
		b++;

		cpus[i] = 1000 - atoi(b);
		if (cpus[i] < 0) cpus[i] = 0;
		buffer = strchr(b, '\n');
	}

	fclose(f);

}

static int do_once(void) {
	/* Read the entries in the directory */
	list_t * ents_list = list_create();
	process_ents = hashmap_create_int(10);

	/* Open the directory */
	DIR * dirp = opendir("/proc");
	struct dirent * dent = readdir(dirp);
	while (dent != NULL) {
		if (dent->d_name[0] >= '0' && dent->d_name[0] <= '9') {
			struct process * p = process_entry(dent);
			if (p) {
				list_insert(ents_list, (void *)p);
			}
		}

		dent = readdir(dirp);
	}
	closedir(dirp);

	size_t count = ents_list->length;
	struct process ** processList = malloc(sizeof(struct process*) * count);
	size_t ent = 0;
	while (ents_list->length) {
		node_t * node = list_pop(ents_list);
		processList[ent] = node->value;
		free(node);
		ent++;
	}

	free(ents_list);

	/* Sort */
	qsort(processList, count, sizeof(struct process*), sort_processes);

	struct winsize w;
	ioctl(STDERR_FILENO, TIOCGWINSZ, &w);

	printf("\033[H");
	int mem_total = 0, mem_used = 0;
	get_mem_info(&mem_total, &mem_used);

	int cpus[32];
	get_cpu_info(cpus);

	int top_rows = 1 + cpu_count;
	int meter_width = w.ws_col / 2;
	int current_row = 0;
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
			snprintf(info_row[0], 99, "\033[94mHostname: \033[0;1m%.*s\033[0m", info_width - 10, tmp);
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
			snprintf(info_row[1], 99, "\033[94mTime: \033[0;1m%.*s\033[0m", info_width - 6, tmp);
		}
		if (top_rows >= 3) {
			info_rows = 3;
			snprintf(info_row[2], 99, "\033[94mTasks: \033[0;1m%lu\033[0m", count);
		}
	}

	int left_side = 1;

	for (int cpu = 0; cpu < cpu_count; ++cpu) {
		char name[20], usage[30];
		sprintf(name, "%3d", cpu + 1);
		sprintf(usage, "%d.%01d%%", cpus[cpu] / 10, cpus[cpu] % 10);
		print_meter(name, usage, left_side ? meter_width : info_width, cpus[cpu], 1000);

		if (current_row < info_rows) {
			printf("%s\033[K\n", info_row[current_row]);
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

	char memUsed[30];
	sprintf(memUsed, "%dM/%dM", mem_used / 1024, mem_total / 1024);
	print_meter("Mem", memUsed, left_side ? meter_width : info_width, mem_used, mem_total);

	if (left_side && current_row < info_rows) {
		printf("%s", info_row[current_row]);
	}
	current_row++;

	printf("\033[K\n");

	print_header();

	int i = 0;

	for (ent = 0; ent < count; ++i, ++ent) {
		if (i < w.ws_row - current_row - 2) print_entry(processList[ent], w.ws_col);
		if (processList[ent]->command_line) free(processList[ent]->command_line);
		if (processList[ent]->process) free(processList[ent]->process);
		free(processList[ent]);
	}

	for (; i < w.ws_row - current_row - 2; ++i) {
		printf("\033[K\n");
	}

	free(processList);

	hashmap_free(process_ents);
	free(process_ents);

	struct pollfd fds[1];
	fds[0].fd = STDIN_FILENO;
	fds[0].events = POLLIN;
	int ret = poll(fds,1,1000);
	if (ret > 0 && fds[0].revents & POLLIN) {
		int c = fgetc(stdin);
		if (c == 'q') return 0;
	}

	return 1;
}

struct termios old;
void get_initial_termios(void) {
	tcgetattr(STDOUT_FILENO, &old);
}

void set_unbuffered(void) {
	struct termios new = old;
	new.c_iflag &= (~ICRNL) & (~IXON);
	new.c_lflag &= (~ICANON) & (~ECHO) & (~ISIG);
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &new);
	printf("\033[?1049h\033[?25l\033[H\033[2J");
}

void set_buffered(void) {
	printf("\033[H\033[2J\033[?25h\033[?1049l");
	tcsetattr(STDOUT_FILENO, TCSAFLUSH, &old);
}

int main (int argc, char * argv[]) {
	show_all = 1;
	show_threads = 0;
	show_cpu = 1;
	show_mem = 1;
	show_username = 1;
	collect_commandline = 1;

	cpu_count = sysfunc(TOARU_SYS_FUNC_NPROC, NULL);

	get_initial_termios();
	set_unbuffered();
	while (do_once());
	set_buffered();

	return 0;
}
