#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

static volatile pthread_key_t _pthread_next_key = 0;

struct _PthreadTlsList {
	size_t count;
	void * values[];
};

__attribute__((tls_model("initial-exec")))
__thread struct _PthreadTlsList * _pthread_data = NULL;

int pthread_key_create(pthread_key_t *key, void (*destructor)(void*)) {
	(void)destructor;
	int me = _pthread_next_key++;
	*key = me;
	return 0;
}

int pthread_key_delete(pthread_key_t key) {
	if (!_pthread_data || key >= _pthread_next_key) return EINVAL;
	if (key >= _pthread_data->count) return 0;
	_pthread_data->values[key] = NULL;
	return 0;
}

void * pthread_getspecific(pthread_key_t key) {
	if (key >= _pthread_next_key) return NULL;
	if (!_pthread_data) return NULL;
	if (key >= _pthread_data->count) return NULL;
	return _pthread_data->values[key];
}

int pthread_setspecific(pthread_key_t key, const void * value) {
	pthread_key_t current_key_max = _pthread_next_key;
	if (key >= current_key_max) return EINVAL;

	if (!_pthread_data) {
		_pthread_data = calloc(sizeof(struct _PthreadTlsList) + sizeof(void*) * current_key_max, 1);
		_pthread_data->count = current_key_max;
	} else if (_pthread_data->count <= key) {
		_pthread_data = realloc(_pthread_data,sizeof(struct _PthreadTlsList) + sizeof(void*) * current_key_max);
		for (size_t i = _pthread_data->count; i < current_key_max; ++i) {
			_pthread_data->values[i] = NULL;
		}
		_pthread_data->count = current_key_max;
	}
	_pthread_data->values[key] = (void*)value;
	return 0;
}
