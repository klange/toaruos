/* vim: tabstop=4 shiftwidth=4 noexpandtab
 *
 * Graphics library
 */

#include <syscall.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "compositing.h"

#ifndef syscall_shm_obtain
DEFN_SYSCALL2(shm_obtain, 35, char *, int)
#endif
#ifndef syscall_shm_release
DEFN_SYSCALL1(shm_release, 36, char *)
#endif
#ifndef syscall_share_fd
DEFN_SYSCALL2(share_fd, 39, int, int)
#endif
#ifndef syscall_share_fd
DEFN_SYSCALL1(get_fd, 40, unsigned int)
#endif

#define LOCK(lock) while (__sync_lock_test_and_set(&lock, 0x01));
#define UNLOCK(lock) __sync_lock_release(&lock);

volatile wins_server_global_t * wins_globals;

/* Internal status */
static struct {
	uint8_t connected;
	int event_pipe;
	int command_pipe;
} wins_status;

int wins_connect() {
	wins_globals = (volatile wins_server_global_t *)syscall_shm_obtain(WINS_SERVER_IDENTIFIER, sizeof(wins_server_global_t));

	/* Verify magic */
	if (wins_globals->magic != WINS_MAGIC) {
		fprintf(stderr, "    Window Server - Client\n");
		fprintf(stderr, "    Connection failed: Bad magic on server shared memory.\n");
		fprintf(stderr, "    Expected %x, got %x\n", WINS_MAGIC, wins_globals->magic);
		return 1;
	}

	/* Enter handshake lock */
	LOCK(wins_globals->lock);
	/* Lock Obtained */

	/* Share client PID */
	wins_globals->client_pid = getpid();
	wins_globals->server_done = 0;

	/* Mark us as done and wait for the server */
	wins_globals->client_done = 1;
	while (!wins_globals->server_done);

	int tmp;
	tmp = wins_globals->event_pipe;
	wins_status.event_pipe   = syscall_get_fd(tmp);
	tmp = wins_globals->command_pipe;
	wins_status.command_pipe = syscall_get_fd(tmp);

	/* Reset client status for next client */
	wins_globals->event_pipe = 0;
	wins_globals->command_pipe = 0;
	wins_globals->client_done = 0;

	/* Done with lock */
	UNLOCK(wins_globals->lock);
	wins_status.connected = 1;
	return 0;
}

int wins_disconnect() {
	/* XXX: shm_release, tell server to close windows, etc? */
}
