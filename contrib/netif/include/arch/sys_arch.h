#ifndef __ARCH_SYS_ARCH_H__
#define __ARCH_SYS_ARCH_H__

#define SYS_MBOX_NULL NULL
#define SYS_SEM_NULL  NULL

typedef u32_t sys_prot_t;

struct sys_sem;
typedef struct sys_sem * sys_sem_t;
#define sys_sem_valid(sem) (((sem) != NULL) && (*(sem) != NULL))
#define sys_sem_set_invalid(sem) do { if((sem) != NULL) { *(sem) = NULL; }}while(0)

/* let sys.h use binary semaphores for mutexes */
#define LWIP_COMPAT_MUTEX 1

struct sys_mbox;
typedef struct sys_mbox *sys_mbox_t;
#define sys_mbox_valid(mbox) (((mbox) != NULL) && (*(mbox) != NULL))
#define sys_mbox_set_invalid(mbox) do { if((mbox) != NULL) { *(mbox) = NULL; }}while(0)

struct sys_thread;
typedef struct sys_thread * sys_thread_t;

#endif /* __ARCH_SYS_ARCH_H__ */
