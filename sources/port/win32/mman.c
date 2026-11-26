#define WIN32_LEAN_AND_MEAN
#include "sys/mman.h"
#include <errno.h>
#include <windows.h>

DWORD __cast_from_prot_to_win(int prot)
{
	if (prot & PROT_WRITE) {
		return PAGE_READWRITE;
	}
	if (prot & PROT_READ) {
		return PAGE_READONLY;
	}
	return PAGE_NOACCESS;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{

	if (length == 0 || fd != -1 || !(flags&MAP_ANONYMOUS)) {
		return MAP_FAILED;
	}
	DWORD winflags = __cast_from_prot_to_win(prot);
	void* map = VirtualAlloc(addr,length,MEM_COMMIT|MEM_RESERVE, winflags);
	if (map!=NULL) {
		return map;
	}
	else {
		errno = ENOMEM;
		return MAP_FAILED;
	}
}

int mprotect(void *addr, size_t length, int prot)
{
	DWORD winflags = __cast_from_prot_to_win(prot);
	DWORD old;
	BOOL map = VirtualProtect(addr, length, winflags, &old);
	if (map != 0) {
		return 0;
	}
	else {
		errno = EINVAL;
		return -1;
	}

}
int munmap(void *addr, size_t length)
{
	BOOL map = VirtualFree(addr, 0, MEM_RELEASE);
	if (map != 0) {
		return 0;
	}
	else {
		errno = EINVAL;
		return -1;
	}

}