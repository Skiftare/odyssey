#pragma once

#include <sys/types.h>
#include <stddef.h>

#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_NONE 0

#define MAP_PRIVATE 16
#define MAP_ANONYMOUS 32

#define MAP_FAILED ((void *)-1)


void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int mprotect(void *addr, size_t length, int prot);
int munmap(void *addr, size_t length);
