#pragma once
#include_next <unistd.h>
static inline int getuid(void)
{
	return 0;
}
