#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include <sys/time.h>

/* This is largely incorrect as alarms are supposed to be retained
 * after exec, and there's potential race conditions involved here,
 * but it's a viable workaround for alarm() to work at all. */

static pthread_t __libc_alarm_thread = 0;
static unsigned int __libc_last_alarm = 0;

static void * __libc_alarm_func(void * arg) {
	unsigned int secs = (uintptr_t)arg;
	sleep(secs); /* XXX What if this gets interrupted? */
	kill(getpid(), SIGALRM);
	__libc_last_alarm = 0;
	return NULL;
}

unsigned int alarm(unsigned int seconds) {
	unsigned int ret = 0;
	struct timeval now;
	gettimeofday(&now, NULL);
	if (__libc_alarm_thread && __libc_last_alarm) {
		pthread_kill(__libc_alarm_thread, SIGKILL);
		ret = __libc_last_alarm - now.tv_sec;
	}
	__libc_last_alarm = now.tv_sec + seconds;
	pthread_create(&__libc_alarm_thread, NULL, __libc_alarm_func, (void*)(uintptr_t)seconds);
	pthread_detach(__libc_alarm_thread);
	return ret;
}
