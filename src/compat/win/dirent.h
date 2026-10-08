// dirent.h for the Windows build: opendir, readdir and closedir, with
// UTF-8 names (posix.cpp).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct dirent
{
	char d_name[1024];
};

typedef struct DmDir DIR;

DIR* opendir(const char* path);
struct dirent* readdir(DIR* dir);
int closedir(DIR* dir);

#ifdef __cplusplus
}
#endif
