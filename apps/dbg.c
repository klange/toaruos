/**
 * @brief Debugger.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2021 K. Lange
 */
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <ctype.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/signal_defs.h>
#include <sys/sysfunc.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <syscall_nums.h>

#include <toaru/rline.h>
#include <toaru/hashmap.h>
#include <kernel/elf.h>

static char * last_command = NULL;
static char * binary_path = NULL;
static FILE * binary_obj = NULL;
static pid_t  binary_pid = 0;
static int    binary_is_child = 0;

#if defined(__x86_64__)
#include <kernel/arch/x86_64/regs.h>
static void dump_regs(struct regs * r) {
	fprintf(stdout,
		"  $rip=0x%016lx\n"
		"  $rsi=0x%016lx,$rdi=0x%016lx,$rbp=0x%016lx,$rsp=0x%016lx\n"
		"  $rax=0x%016lx,$rbx=0x%016lx,$rcx=0x%016lx,$rdx=0x%016lx\n"
		"  $r8= 0x%016lx,$r9= 0x%016lx,$r10=0x%016lx,$r11=0x%016lx\n"
		"  $r12=0x%016lx,$r13=0x%016lx,$r14=0x%016lx,$r15=0x%016lx\n"
		"  cs=0x%016lx  ss=0x%016lx rflags=0x%016lx int=0x%02lx err=0x%02lx\n",
		r->rip,
		r->rsi, r->rdi, r->rbp, r->rsp,
		r->rax, r->rbx, r->rcx, r->rdx,
		r->r8, r->r9, r->r10, r->r11,
		r->r12, r->r13, r->r14, r->r15,
		r->cs, r->ss, r->rflags, r->int_no, r->err_code
	);
}
#define regs_ip(regs) ((regs)->rip)
#define regs_bp(regs) ((regs)->rbp)
#elif defined(__aarch64__)
#define regs _regs
#include <kernel/arch/aarch64/regs.h>
#undef regs
struct regs {
	struct _regs gp;
	uint64_t elr;
};
static void dump_regs(struct regs *r) {
#define reg(a,b) printf(" $x%02d=0x%016lx $x%02d=0x%016lx\n",a,r->gp.x ## a, b, r->gp.x ## b)
	reg(0,1);
	reg(2,3);
	reg(4,5);
	reg(6,7);
	reg(8,9);
	reg(10,11);
	reg(12,13);
	reg(14,15);
	reg(16,17);
	reg(18,19);
	reg(20,21);
	reg(22,23);
	reg(24,25);
	reg(26,27);
	reg(28,29);
	printf(" $x30=0x%016lx  sp=0x%016lx\n", r->gp.x30, r->gp.user_sp);
	printf(" elr=0x%016lx\n", r->elr);
#undef reg
#define regs_ip(regs) ((regs)->elr)
#define regs_bp(regs) ((regs)->gp.x29)
}
#else
# error "Unsupported arch"
#endif

#define M(e) [e] = #e
const char * signal_names[256] = {
	M(SIGHUP),
	M(SIGINT),
	M(SIGQUIT),
	M(SIGILL),
	M(SIGTRAP),
	M(SIGABRT),
	M(SIGEMT),
	M(SIGFPE),
	M(SIGKILL),
	M(SIGBUS),
	M(SIGSEGV),
	M(SIGSYS),
	M(SIGPIPE),
	M(SIGALRM),
	M(SIGTERM),
	M(SIGUSR1),
	M(SIGUSR2),
	M(SIGCHLD),
	M(SIGPWR),
	M(SIGWINCH),
	M(SIGURG),
	M(SIGPOLL),
	M(SIGSTOP),
	M(SIGTSTP),
	M(SIGCONT),
	M(SIGTTIN),
	M(SIGTTOUT),
	M(SIGVTALRM),
	M(SIGPROF),
	M(SIGXCPU),
	M(SIGXFSZ),
	M(SIGWAITING),
	M(SIGDIAF),
	M(SIGHATE),
	M(SIGWINEVENT),
	M(SIGCAT),
};

static int data_read_bytes(pid_t pid, uintptr_t addr, char * buf, size_t size) {
	for (unsigned int i = 0; i < size; ++i) {
		if (ptrace(PTRACE_PEEKDATA, pid, (void*)addr++, &buf[i])) {
			return 1;
		}
	}
	return 0;
}

static int data_read_int(pid_t pid, uintptr_t addr) {
	int x;
	data_read_bytes(pid, addr, (char*)&x, sizeof(int));
	return x;
}

static uintptr_t data_read_ptr(pid_t pid, uintptr_t addr) {
	uintptr_t x;
	data_read_bytes(pid, addr, (char*)&x, sizeof(uintptr_t));
	return x;
}

static void string_arg(pid_t pid, uintptr_t ptr, size_t maxsize) {
	FILE * logfile = stdout;

	if (ptr == 0) {
		fprintf(logfile, "NULL");
		return;
	}

	fprintf(logfile, "\"");

	size_t size = 0;
	uint8_t buf = 0;

	do {
		long result = ptrace(PTRACE_PEEKDATA, pid, (void*)ptr, &buf);
		if (result != 0) break;
		if (!buf) {
			fprintf(logfile, "\"");
			return;
		}

		if (buf == '\\') fprintf(logfile, "\\\\");
		else if (buf == '"') fprintf(logfile, "\\\"");
		else if (buf >= ' ' && buf < '~') fprintf(logfile, "%c", buf);
		else if (buf == '\r') fprintf(logfile, "\\r");
		else if (buf == '\n') fprintf(logfile, "\\n");
		else fprintf(logfile, "\\x%02x", buf);

		ptr++;
		size++;
		if (size > maxsize) break;
	} while (buf);

	fprintf(logfile, "\"...");
}

extern uintptr_t __ld_symbol_table(void);
extern uintptr_t __ld_objects_table(void);

static char * read_string(pid_t pid, uintptr_t ptr) {
	if (!ptr) return strdup("(null)");
	size_t len = 0;
	uint8_t buf = 0;
	while ((data_read_bytes(pid, ptr + len, (char*)&buf, 1), buf)) len++;

	char * out = malloc(len + 1);
	data_read_bytes(pid, ptr, out, len+1);
	return out;
}

typedef struct elf_object {
	FILE * file;
	Elf64_Header header;
	char * dyn_string_table;
	size_t dyn_string_table_size;
	Elf64_Sym * dyn_symbol_table;
	size_t dyn_symbol_table_size;
	Elf64_Dyn * dynamic;
	Elf64_Word * dyn_hash;
	void (*init)(void);
	void (**init_array)(void);
	size_t init_array_size;
	uintptr_t base;
	list_t * dependencies;
	int loaded;
} elf_t;

static int find_symbol(pid_t pid, uintptr_t addr_in, char ** name, uintptr_t *addr_out, char ** objname) {

	intptr_t  current_max = INTPTR_MAX;
	uintptr_t current_addr = NULL;
	uintptr_t current_xname = NULL;
	char * current_name = NULL;
	char * current_obj = NULL;
	uintptr_t best_base = 0;

	/* Can we cheat and peek at ld.so? */
	uintptr_t their_symbol_table  = data_read_ptr(pid, __ld_symbol_table());

	if (their_symbol_table) {
		hashmap_t map;
		data_read_bytes(pid, their_symbol_table, (char*)&map, sizeof(hashmap_t));

		/* Cool, now let's look at every entry... */
		for (size_t i = 0; i < map.size; ++i) {
			uintptr_t ptr;
			hashmap_entry_t entry;
			data_read_bytes(pid, (uintptr_t)map.entries + sizeof(uintptr_t) * i, (char*)&ptr, sizeof(uintptr_t));
			int j = 0;
			while (ptr) {
				data_read_bytes(pid, ptr, (char*)&entry, sizeof(hashmap_entry_t));
				if (entry.value && addr_in >= (uintptr_t)entry.value) {
					intptr_t x = addr_in - (uintptr_t)entry.value;
					if (x < current_max) {
						current_max = x;
						current_addr = (uintptr_t)entry.value;
						current_xname = (uintptr_t)entry.key;
					}
				}
				ptr = (uintptr_t)entry.next;
				j++;
			}
		}

		if (current_xname) {
			current_name = read_string(pid, current_xname);
		}
	}

	if (addr_in < 0x40000000) {
		current_obj = strdup("ld.so");
	}

	/* Figure out where this object is in the objects map */
	uintptr_t their_objects_table = data_read_ptr(pid, __ld_objects_table());

	if (!current_obj && their_objects_table) {
		hashmap_t map;
		data_read_bytes(pid, their_objects_table, (char*)&map, sizeof(hashmap_t));

		intptr_t  cmax = INTPTR_MAX;
		uintptr_t best_name = 0;
		for (size_t i = 0; i < map.size; ++i) {
			uintptr_t ptr;
			hashmap_entry_t entry;
			data_read_bytes(pid, (uintptr_t)map.entries + sizeof(uintptr_t) * i, (char*)&ptr, sizeof(uintptr_t));
			while (ptr) {
				data_read_bytes(pid, ptr, (char*)&entry, sizeof(hashmap_entry_t));
				if (entry.value) {
					elf_t obj;
					data_read_bytes(pid, (uintptr_t)entry.value, (char*)&obj, sizeof(elf_t));
					if (addr_in >= obj.base) {
						intptr_t x = addr_in - (uintptr_t)obj.base;
						if (x < cmax) {
							cmax = x;
							best_name = (uintptr_t)entry.key;
							best_base = obj.base;
						}
					}
				}
				ptr = (uintptr_t)entry.next;
			}
		}

		if (best_name) {
			current_obj = read_string(pid, best_name);
		}
	}

	FILE * f = binary_obj;
	if (current_obj) {
		/* Try to open that */
		struct stat stat_buf;
		char path[1024];
		sprintf(path, "/lib/%s", current_obj);
		if (stat(path, &stat_buf)) {
			sprintf(path, "/usr/lib/%s", current_obj);
			if (stat(path, &stat_buf)) goto _bail;
		}

		f = fopen(path, "r");
	} else {
_bail:
		current_obj = strdup(binary_path);
		best_base = 0;
	}

	fseek(f, 0, SEEK_SET);
	Elf64_Header header;
	fread(&header, sizeof(Elf64_Header), 1, f);

	for (unsigned int i = 0; i < header.e_shnum; ++i) {
		fseek(f, header.e_shoff + header.e_shentsize * i, SEEK_SET);
		Elf64_Shdr sectionHeader;
		fread(&sectionHeader, sizeof(Elf64_Shdr), 1, f);
		switch (sectionHeader.sh_type) {
			case SHT_SYMTAB:
			case SHT_DYNSYM: {
				/* Try to get the actual one if possible */
				Elf64_Sym * symtab = malloc(sectionHeader.sh_size);

				if (sectionHeader.sh_addr > 0x40000000) {
					data_read_bytes(pid, sectionHeader.sh_addr, (char*)symtab, sectionHeader.sh_size);
				} else {
					fseek(f, sectionHeader.sh_offset, SEEK_SET);
					fread(symtab, sectionHeader.sh_size, 1, f);
				}

				Elf64_Shdr shdr_strtab;
				fseek(f, header.e_shoff + header.e_shentsize * sectionHeader.sh_link, SEEK_SET);
				fread(&shdr_strtab, sizeof(Elf64_Shdr), 1, f);
				char * strtab = malloc(shdr_strtab.sh_size);
				fseek(f, shdr_strtab.sh_offset, SEEK_SET);
				fread(strtab, shdr_strtab.sh_size, 1, f);

				for (unsigned int i = 0; i < sectionHeader.sh_size / sizeof(Elf64_Sym); ++i) {
					if (!symtab[i].st_value) continue;
					if ((symtab[i].st_info & 0xF) == STT_SECTION) continue;
					if ((symtab[i].st_info & 0xF) == STT_NOTYPE) continue;
					if (addr_in >= ((uintptr_t)symtab[i].st_value + best_base)) {
						intptr_t x = addr_in - ((uintptr_t)symtab[i].st_value + best_base);
						if (x < current_max) {
							if (current_name) free(current_name);
							current_max = x;
							current_addr = symtab[i].st_value + best_base;
							current_name = strdup(strtab + symtab[i].st_name);
						}
					}
				}

				free(strtab);
				free(symtab);
			}
			break;
		}
	}

	*addr_out = current_addr;
	*name = current_name;
	*objname = current_obj;

	if (current_name) return 1;
	if (f != binary_obj) fclose(f);
	return 0;
}

static void show_libs(pid_t pid) {
	hashmap_t map;
	uintptr_t their_objects_table = data_read_ptr(pid, __ld_objects_table());
	data_read_bytes(pid, their_objects_table, (char*)&map, sizeof(hashmap_t));
	for (size_t i = 0; i < map.size; ++i) {
		uintptr_t ptr;
		hashmap_entry_t entry;
		data_read_bytes(pid, (uintptr_t)map.entries + sizeof(uintptr_t) * i, (char*)&ptr, sizeof(uintptr_t));
		while (ptr) {
			data_read_bytes(pid, ptr, (char*)&entry, sizeof(hashmap_entry_t));
			if (entry.value) {
				elf_t obj;
				data_read_bytes(pid, (uintptr_t)entry.value, (char*)&obj, sizeof(elf_t));
				char * s = read_string(pid, (uintptr_t)entry.key);
				fprintf(stderr, "%s @ %#zx\n", s, (uintptr_t)obj.base);
			}
			ptr = (uintptr_t)entry.next;
		}
	}
}

static void attempt_backtrace(pid_t pid, struct regs * regs) {

	/* We already printed the top, now let's try to dig down */
	uintptr_t ip = regs_ip(regs);
	uintptr_t bp = regs_bp(regs);
	int depth = 0;
	int max_depth = 20;

	while (bp && ip && depth < max_depth && ip < 0xFFFfff0000000000UL) {
		char * name = NULL;
		char * objname = NULL;
		uintptr_t addr = 0;
		if (find_symbol(pid, ip - 1, &name, &addr, &objname)) {
			fprintf(stderr, "<0x%016zx> %s+%#zx in %s\n",
				ip,
				name, ip - addr, objname);
			free(name);
			free(objname);
		}

		ip = data_read_ptr(pid, bp + sizeof(uintptr_t));
		bp = data_read_ptr(pid, bp);
		depth++;
	}
}

static int imatch(const char * a, const char * b) {
	do {
		if (!*a && !*b) return 1;
		if (tolower(*a) != tolower(*b)) return 0;
		a++;
		b++;
	} while (1);
}

static int signal_from_string(const char * str) {
	if (isdigit(*str)) {
		return strtoul(str,NULL,0);
	} else if (str[0] == 'S' && str[1] == 'I' && str[2] == 'G') {
		for (int i = 0; i < 256; ++i) {
			if (signal_names[i] && imatch(signal_names[i], str)) return i;
		}
		return -1;
	} else {
		for (int i = 0; i < 256; ++i) {
			if (signal_names[i] && imatch(signal_names[i]+3, str)) return i;
		}
		return -1;
	}

	return -1;
}

static void show_commandline(pid_t pid, int status, struct regs * regs) {

	fprintf(stderr, "[Process %d, ip=%#zx]\n",
		pid, regs_ip(regs));

	/* Try to figure out what symbol that is */
	char * name = NULL;
	char * objname = NULL;
	uintptr_t addr = 0;
	if (find_symbol(pid, regs_ip(regs), &name, &addr, &objname)) {
		fprintf(stderr, "     %s+%zx in %s\n",
			name, regs_ip(regs) - addr, objname);
		free(name);
		free(objname);
	}

	while (1) {
		char buf[4096] = {0};
		rline_exit_string = "";
		rline_exp_set_prompts("(dbg) ", "", 6, 0);
		rline_exp_set_syntax("dbg");
		rline_exp_set_tab_complete_func(NULL); /* TODO */
		if (rline(buf, 4096) == 0) goto _exitDebugger;

		char *nl = strstr(buf, "\n");
		if (nl) *nl = '\0';
		if (!strlen(buf)) {
			if (last_command) {
				strcpy(buf, last_command);
			} else {
				continue;
			}
		} else {
			rline_history_insert(strdup(buf));
			rline_scroll = 0;
			if (last_command) free(last_command);
			last_command = strdup(buf);
		}

		/* Tokenize just the first command */
		char * arg = NULL;
		char * sp = strstr(buf, " ");
		if (sp) {
			*sp = '\0';
			arg = sp + 1;
		}

		if (!strcmp(buf, "show")) {
			if (!arg) {
				fprintf(stderr, "Things that can be shown:\n");
				fprintf(stderr, "   regs\n");
				fprintf(stderr, "   libs\n");
				continue;
			}

			if (!strcmp(arg, "regs")) {
				dump_regs(regs);
			} else if (!strcmp(arg, "libs")) {
				show_libs(pid);
			} else {
				fprintf(stderr, "Don't know how to show '%s'\n", arg);
			}
		} else if (!strcmp(buf, "bt") || !strcmp(buf, "backtrace")) {
			attempt_backtrace(pid, regs);
		} else if (!strcmp(buf, "continue") || !strcmp(buf,"c")) {
			int signum = WSTOPSIG(status);
			if (signum == SIGINT) signum = 0;
			ptrace(PTRACE_CONT, pid, NULL, (void*)(uintptr_t)signum);
			return;
		} else if (!strcmp(buf, "signal")) {
			if (!arg) {
				fprintf(stderr, "'signal' needs an argument\n");
				continue;
			}
			int signum = signal_from_string(arg);
			if (signum == -1) {
				fprintf(stderr, "'%s' is not a recognized signal\n", arg);
				continue;
			}
			ptrace(PTRACE_CONT, pid, NULL, (void*)(uintptr_t)signum);
			return;
		} else if (!strcmp(buf, "step") || !strcmp(buf,"s")) {
			int signum = WSTOPSIG(status);
			if (signum == SIGINT) signum = 0;
			ptrace(PTRACE_SINGLESTEP, pid, NULL, (void*)(uintptr_t)signum);
			return;
		} else if (!strcmp(buf, "poke")) {
			char * addr = arg;
			char * data = strstr(addr, " ");
			if (!data) {
				fprintf(stderr, "usage: poke addr byte\n");
				continue;
			}
			*data = '\0'; data++;

			uintptr_t addr_ = strtoul(addr, NULL, 0);
			uintptr_t data_ = strtoul(data, NULL, 0);

			if (ptrace(PTRACE_POKEDATA, pid, (void*)addr_, (void*)&data_) != 0) {
				fprintf(stderr, "poke: %s\n", strerror(errno));
				continue;
			}

		} else if (!strcmp(buf, "print") || !strcmp(buf,"p")) {
			char * fmt = arg;
			char * sp = strstr(arg, " ");
			if (!sp) {
				fprintf(stderr, "usage: print fmt addr\n");
				continue;
			}
			*sp = '\0'; sp++;

			uintptr_t addr = strtoul(sp,NULL,0);

			/* Parse any leading numbers */
			int count = 1;

			if (*fmt >= '1' && *fmt <= '9') {
				count = (*fmt - '0');
				fmt++;
				while (*fmt >= '0' && *fmt <= '9') {
					count *= 10;
					count += (*fmt - '0');
					fmt++;
				}
			}

			/* Parse the format */
			for (int i = 0; i < count; ++i) {
				if (!strcmp(fmt, "x")) {
					uint8_t buf[1];
					data_read_bytes(pid, addr, (char*)buf, 1);
					printf("%02x", buf[0]);
					addr += 1;
				} else if (!strcmp(fmt, "i")) {
					printf("%d", data_read_int(pid,addr));
					addr += sizeof(int);
				} else if (!strcmp(fmt, "l")) {
					printf("%ld", (intptr_t)data_read_ptr(pid,addr));
					addr += sizeof(long);
				} else if (!strcmp(fmt, "p")) {
					printf("%#zx", data_read_ptr(pid,addr));
					addr += sizeof(uintptr_t);
				} else if (!strcmp(fmt, "s")) {
					string_arg(pid,addr,count == 1 ? 30 : count);
					break;
				} else {
					printf("print: invalid format string");
					break;
				}
				if (i + 1 < count) {
					printf(" ");
				}
			}
			printf("\n");
		} else if (!strcmp(buf, "help")) {
			printf("commands:\n"
				"  show (regs, libs)\n"
				"  backtrace\n"
				"  continue\n"
				"  signal signum\n"
				"  step\n"
				"  poke addr byte\n"
				"  print fmt addr\n");
			continue;
		} else {
			fprintf(stderr, "dbg: unrecognized command '%s'\n", buf);
			continue;
		}
	}

_exitDebugger:
	if (binary_is_child) {
		fprintf(stderr, "Terminating child process '%d'.\n", pid);
		ptrace(PTRACE_DETACH, pid, NULL, (void*)(uintptr_t)SIGKILL);
	}
	exit(0);
}

static int usage(char * argv[]) {
#define T_I "\033[3m"
#define T_O "\033[0m"
	fprintf(stderr, "usage: %s command...\n"
			"  -h         " T_I "Show this help text." T_O "\n",
			argv[0]);
	return 1;
}

#define DEFAULT_PATH "/bin:/usr/bin"
static char * find_binary(const char * file) {
	if (file && (!strstr(file, "/"))) {
		char * path = getenv("PATH");
		if (!path) {
			path = DEFAULT_PATH;
		}
		char * xpath = strdup(path);
		char * p, * last;
		for ((p = strtok_r(xpath, ":", &last)); p; p = strtok_r(NULL, ":", &last)) {
			int r;
			struct stat stat_buf;
			char * exe = malloc(strlen(p) + strlen(file) + 2);
			strcpy(exe, p);
			strcat(exe, "/");
			strcat(exe, file);
			r = stat(exe, &stat_buf);
			if (r != 0) {
				continue;
			}
			if (!(stat_buf.st_mode & 0111)) {
				continue;
			}
			free(xpath);
			return exe;
		}
		free(xpath);
		return NULL;
	} else if (file) {
		return strdup(file);
	}
	return NULL;
}

static char * sig_to_str(int signum) {
	static char _buf[100];
	if (signum >= 0 && signum <= 255) {
		char * maybe = (char*)signal_names[signum];
		if (maybe) {
			return maybe;
		}
	}
	sprintf(_buf, "%d", signum);
	return _buf;
}

static void pass_sig(int sig) {
	kill(binary_pid, sig);
	signal(SIGINT, pass_sig);
}

int main(int argc, char * argv[]) {
	pid_t target_pid = 0;
	int opt;
	while ((opt = getopt(argc, argv, "o:p:h")) != -1) {
		switch (opt) {
			case 'p':
				target_pid = atoi(optarg);
				break;
			case 'h':
				return (usage(argv), 0);
			case '?':
				return usage(argv);
		}
	}

	if (optind == argc) {
		return usage(argv);
	}

	/* TODO find argv[optind] */
	/* TODO load symbols from it, and from its dependencies... with offsets... from ld.so... */

	binary_path = find_binary(argv[optind]);

	if (!binary_path) {
		fprintf(stderr, "%s: %s: No such file or not an executable.\n",
			argv[0], argv[optind]);
		return 1;
	}

	binary_obj = fopen(binary_path, "r");

	if (!binary_obj) {
		fprintf(stderr, "%s: %s: %s\n",
			argv[0], argv[optind], strerror(errno));
		return 1;
	}

	/* Attempt to load symbol information... */

	if (target_pid) {
		binary_pid = target_pid;
		if (ptrace(PTRACE_ATTACH, binary_pid, NULL, NULL) < 0) {
			fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
			return 1;
		}
		signal(SIGINT, pass_sig);
	} else {
		binary_is_child = 1;
		binary_pid = fork();
		if (!binary_pid) {
			if (ptrace(PTRACE_TRACEME, 0, NULL, NULL) < 0) {
				fprintf(stderr, "%s: ptrace: %s\n", argv[0], strerror(errno));
				return 1;
			}
			execv(binary_path, &argv[optind]);
			return 1;
		}

		signal(SIGINT, SIG_IGN);
	}

	while (1) {
		int status = 0;
		pid_t res = waitpid(binary_pid, &status, WSTOPPED);

		if (res == 0) continue;

		if (res < 0) {
			if (errno == EINTR) continue;
			fprintf(stderr, "%s: waitpid: %s\n", argv[0], strerror(errno));
		} else {
			if (WIFSTOPPED(status)) {
				if (WSTOPSIG(status) == SIGTRAP) {
					/* Don't care about TRAP right now */
					int event = (status >> 16) & 0xFF;
					switch (event) {
						case PTRACE_EVENT_SINGLESTEP: {
								struct regs regs;
								ptrace(PTRACE_GETREGS, res, NULL, &regs);
								show_commandline(res, status, &regs);
							}
							break;
						default:
							//ptrace(PTRACE_SIGNALS_ONLY_PLZ, p, NULL, NULL);
							ptrace(PTRACE_CONT, res, NULL, NULL);
							break;
					}
				} else {
					printf("Program received signal %s.\n", sig_to_str(WSTOPSIG(status)));

					struct regs regs;
					ptrace(PTRACE_GETREGS, res, NULL, &regs);

					show_commandline(res, status, &regs);
				}
			} else if (WIFSIGNALED(status)) {
				fprintf(stderr, "Process %d was killed by %s.\n", res, signal_names[WTERMSIG(status)]);
				return 0;
			} else if (WIFEXITED(status)) {
				fprintf(stderr, "Process %d exited normally.\n", res);
				return 0;
			} else {
				fprintf(stderr, "Unknown state?\n");
			}
		}
	}
	

	return 0;
}
