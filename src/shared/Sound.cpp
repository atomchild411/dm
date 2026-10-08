#include "Sound.hpp"

#include <cstdlib>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>

#include "Perf.hpp"

namespace
{
	const char* const DEFAULT_SOUND = "/usr/share/data/sounds/soundscheme/soundfiles/08.ting.aifc";
	const char* const PLAYER = "/usr/sbin/sfplay";

	double g_lastSound = 0;

	bool Exists(const char* path)
	{
		struct stat st;
		return path && stat(path, &st) == 0;
	}
}

// the player in the background: forked twice, so nothing waits for it
void Sound::PlayNotification(std::function<void()> fallback)
{
	double now = Perf::Now();
	if (now - g_lastSound < 2.0)
		return; // a burst of mentions: one sound
	g_lastSound = now;

	const char* file = getenv("DM_SOUND");
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
}
