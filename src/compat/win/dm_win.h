// The Windows build includes this first in every C++ file (-include): the
// POSIX names the code uses, on top of the Universal CRT, so the code stays
// written for POSIX.  The headers beside it (unistd.h, dirent.h, strings.h,
// sys/time.h) stand in for the ones Windows lacks; posix.cpp implements them.
//
// Paths are UTF-8 throughout: the program's manifest (windows/) sets the
// process code page to UTF-8, and what posix.cpp adds calls the wide APIs.
#pragma once

#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <sys/stat.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif
// rename() that replaces an existing file, as POSIX's does (RenameOver)
int dm_win_rename(const char* from, const char* to);
#ifdef __cplusplus
}
#endif

// timegm: Windows calls it _mkgmtime
#define timegm(tm) _mkgmtime(tm)

// mkdir(path, mode): Windows has no modes
#define mkdir(path, mode) _mkdir(path)

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
