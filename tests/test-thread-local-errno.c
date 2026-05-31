#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>

static void * child_thread(void *arg) {
	(void)arg;
	for (int j = 0; j < 5; ++j) {
		open("/home/root/gjewakgjewa", O_RDONLY);
		for (int i = 0; i < 5; ++i) {
			assert(errno == EACCES);
			usleep(10000);
		}
	}
	return NULL;
}


int main(int argc, char * argv[]) {
	pthread_t child;
	pthread_create(&child, NULL, child_thread, NULL);

	for (int j = 0; j < 5; ++j) {
		open("/lekwagjelkwajgelwajgewakgjewa", O_RDONLY);
		for (int i = 0; i < 5; ++i) {
			assert(errno == ENOENT);
			usleep(10000);
		}
	}
}
