#ifndef _TOASTD_H_
#define _TOASTD_H_

typedef struct {
	unsigned int ttl;
	char strings[];
} notification_t;

#endif /* _TOASTD_H_ */
