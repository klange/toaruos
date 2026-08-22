#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
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

int shm_open(const char * name, int flag, mode_t mode) {
	if (*name != '/') return -EINVAL;
	char rname[PATH_MAX];
	memcpy(rname,"/dev/shm",8);
	memcpy(rname+8,name,strlen(name)+1);
	return open(rname, flag | O_CLOEXEC, mode);
}

int shm_unlink(const char * name) {
	if (*name != '/') return -EINVAL;
	char rname[PATH_MAX];
	memcpy(rname,"/dev/shm",8);
	memcpy(rname+8,name,strlen(name)+1);
	return unlink(rname);
}
