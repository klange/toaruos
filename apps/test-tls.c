#include <stdio.h>
#include <pthread.h>
#include <sys/sysfunc.h>

__thread int myvalue;

void * getaddressinthread(void * _unused) {
	fprintf(stderr, "in thread before:\n");
	fprintf(stderr, "&myvalue = %p\n", (void*)&myvalue);
	fprintf(stderr, "myvalue  = %d\n", myvalue);
	myvalue = 1234;
	fprintf(stderr, "in thread after:\n");
	fprintf(stderr, "&myvalue = %p\n", (void*)&myvalue);
	fprintf(stderr, "myvalue  = %d\n", myvalue);
	return NULL;
}

int main(int argc, char * argv[]) {

	myvalue = 42;

	fprintf(stderr, "main thread before:\n");
	fprintf(stderr, "&myvalue = %p\n", (void*)&myvalue);
	fprintf(stderr, "myvalue  = %d\n", myvalue);

	pthread_t mythread;
	pthread_create(&mythread, NULL, getaddressinthread, NULL);

	void * retval;
	pthread_join(mythread, &retval);

	fprintf(stderr, "main thread after:\n");
	fprintf(stderr, "&myvalue = %p\n", (void*)&myvalue);
	fprintf(stderr, "myvalue  = %d\n", myvalue);

	return 0;
}
