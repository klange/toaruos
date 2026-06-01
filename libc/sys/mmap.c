#include <sys/mman.h>
#include <libc/syscall.h>
#include <sys/syscall.h>
#include <errno.h>

DEFN_SYSCALL6(mmap, SYS_MMAP, void*, size_t, int, int, int, off_t);

void * mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
	__sets_errno_type(void*,syscall_mmap(addr,length,prot,flags,fd,offset));
}

DEFN_SYSCALL2(munmap, SYS_MUNMAP, void*, size_t);

int munmap(void *addr, size_t length) {
	__sets_errno(syscall_munmap(addr,length));
}
