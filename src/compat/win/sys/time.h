// sys/time.h for the Windows build: struct timeval is Winsock's.
#pragma once

#include <winsock2.h>

#ifdef __cplusplus
extern "C" {
#endif
int gettimeofday(struct timeval* tv, void* tz);
#ifdef __cplusplus
}
#endif
