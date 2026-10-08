// unistd.h for the Windows build: what the code uses of it.
#pragma once

#include <io.h>
#include <process.h>
#include <direct.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#endif

#ifdef __cplusplus
extern "C" {
#endif
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
#ifdef __cplusplus
}
#endif
