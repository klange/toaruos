#include <stdlib.h>

/* Header-only */
#include <toaru/spinlock.h>

#include <libc/internal.h>

_hidden int volatile __atexit_lock = 0;
_hidden void (*__atexit_handlers[32])(void) = {NULL};
_hidden int __atexit_count = 0;

_hidden void __atexit_run(void) {
	spin_lock(&__atexit_lock);
	if (__atexit_count) {
		/* atexit handlers can very well call atexit() themselves,
		 * so we do this awkward loop to ensure we're picking up
		 * any changes to the count and always running the
		 * most recently added ones. */
		do {
			__atexit_count--;
			void (*handler)(void) = __atexit_handlers[__atexit_count];
			spin_unlock(&__atexit_lock);
			handler();
			spin_lock(&__atexit_lock);
		} while (__atexit_count);
	}
	spin_unlock(&__atexit_lock);
}

int atexit(void (*h)(void)) {
	spin_lock(&__atexit_lock);
	if (__atexit_count == ATEXIT_MAX) {
		spin_unlock(&__atexit_lock);
		return -1; /* Spec says "non-zero", but maybe -1 is expected. */
	}
	__atexit_handlers[__atexit_count++] = h;
	spin_unlock(&__atexit_lock);
	return 0;
}

