#include <module.h>
#include <printf.h>
#include <mod/shell.h>

#include "lwip/init.h"

#include "lwip/sys.h"
#include "lwip/opt.h"
#include "lwip/stats.h"

#include "lwip/ip.h"
#include "lwip/ip_frag.h"
#include "lwip/udp.h"
#include "lwip/tcp_impl.h"

#include "netif/etharp.h"

int memcmp(const void *s1, const void *s2, size_t n) {
	const uint8_t *b1 = (const uint8_t *)s1;
	const uint8_t *b2 = (const uint8_t *)s2;
	for (unsigned int i = 0; i < n; ++i) {
		if (*b1 < *b2) return -1;
		if (*b1 > *b2) return 1;
	}
	return 0;
}

sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg, int stacksize, int prio) {
	(void)stacksize;
	(void)prio;
	int pid = create_kernel_tasklet((tasklet_t)thread, (char *)name, arg);
	return (sys_thread_t)process_from_pid(pid);
}

void * heap_external = NULL;

void sys_init(void) {
	debug_print(NOTICE, "lwip sys_init() called");
	heap_external = malloc(16000);
}

struct sys_sem {
	volatile uint8_t i;
};

err_t sys_sem_new(sys_sem_t *sem, u8_t count) {
	sys_sem_t s = malloc(sizeof(struct sys_sem));
	s->i = 0;
	*sem = s;
	return 0;
}

void sys_sem_free(sys_sem_t * sem) {
	free(*sem);
}

void sys_sem_signal(sys_sem_t *sem) {
	spin_unlock(&(*sem)->i);
}

u32_t sys_arch_sem_wait(sys_sem_t * sem, u32_t timeout) {
	int i = now();
	spin_lock(&(*sem)->i);
	return (now() - i) * 1000;
}

struct sys_mbox {
	volatile uint8_t i;
	void * msg;
};

err_t sys_mbox_new(sys_mbox_t *mbox, int size) {
	sys_mbox_t m = malloc(sizeof(struct sys_mbox));
	m->i = 0;
	m->msg = NULL;
	*mbox = m;
	return 0;
}

void sys_mbox_post(sys_mbox_t *mbox, void *msg) {
	spin_lock(&(*mbox)->i);
	(*mbox)->msg = msg;
	spin_unlock(&(*mbox)->i);
}

err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg) {
	spin_lock(&(*mbox)->i);
	(*mbox)->msg = msg;
	spin_unlock(&(*mbox)->i);
	return 0;
}

u32_t sys_arch_mbox_fetch(sys_mbox_t *mbox, void **msg, u32_t timeout) {
	while (1) {
		spin_lock(&(*mbox)->i);
		if ((*mbox)->msg) {
			spin_unlock(&(*mbox)->i);
			*msg = (*mbox)->msg;
			return 0;
		}
		spin_unlock(&(*mbox)->i);
	}
}
u32_t sys_arch_mbox_tryfetch(sys_mbox_t *mbox, void **msg) {
	spin_lock(&(*mbox)->i);
	if ((*mbox)->msg) {
		spin_unlock(&(*mbox)->i);
		*msg = (*mbox)->msg;
		return 0;
	}
	spin_unlock(&(*mbox)->i);
	return SYS_MBOX_EMPTY;
}

void sys_mbox_free(sys_mbox_t *mbox) {
	free(*mbox);
}

static ip_addr_t ipaddr, netmask, gw;

DEFINE_SHELL_FUNCTION(netif_test, "networking stuff") {
	fprintf(tty, "Initializing LWIP...\n");

	IP4_ADDR(&gw, 192,168,0,1);
	IP4_ADDR(&ipaddr, 192,168,0,2);
	IP4_ADDR(&netmask, 255,255,255,0);

	lwip_init();

	fprintf(tty, "LWIP is initialized\n");

	return 0;
}

static int init(void) {
	BIND_SHELL_FUNCTION(netif_test);
	return 0;
}

static int fini(void) {
	return 0;
}

MODULE_DEF(netif, init, fini);
MODULE_DEPENDS(debugshell);
