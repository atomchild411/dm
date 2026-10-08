#include "Sound.hpp"

#include <cstdlib>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#else
#include <sys/wait.h>
#endif

#include "Perf.hpp"

namespace
{
	double g_lastSound = 0;

#ifndef _WIN32
	const char* const DEFAULT_SOUND = "/usr/share/data/sounds/soundscheme/soundfiles/08.ting.aifc";
	const char* const PLAYER = "/usr/sbin/sfplay";

	bool Exists(const char* path)
	{
		struct stat st;
		return path && stat(path, &st) == 0;
	}
#endif
}

// the player in the background: forked twice, so nothing waits for it
void Sound::PlayNotification(std::function<void()> fallback)
{
	double now = Perf::Now();
	if (now - g_lastSound < 2.0)
		return; // a burst of mentions: one sound
	g_lastSound = now;

	const char* file = getenv("DM_SOUND");
#ifdef _WIN32
	// DM_SOUND (a .wav file), or the system's notification sound
	if (file && *file)
		PlaySoundA(file, nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
	else
		PlaySoundW(L"Notification.Default", nullptr, SND_ALIAS | SND_ASYNC);
	(void) fallback;
#else
	if (!file || !*file)
		file = DEFAULT_SOUND;
	if (!Exists(PLAYER) || !Exists(file)) {
		if (fallback)
			fallback();
		return;
	}
	pid_t pid = fork();
	if (pid == 0) {
		if (fork() == 0) {
			int null = open("/dev/null", O_RDWR);
			if (null >= 0) {
				dup2(null, 0);
				dup2(null, 1);
				dup2(null, 2);
			}
			execl(PLAYER, "sfplay", file, (char*) NULL);
			_exit(127);
		}
		_exit(0);
	}
	if (pid > 0)
		waitpid(pid, NULL, 0);
#endif
}
