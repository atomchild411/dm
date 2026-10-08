#pragma once

#include <functional>

// The sound for a mention or a direct message, played in the background:
// DM_SOUND names the file, else the desktop's (on IRIX, sfplay and the
// sound scheme's "ting").  A burst within two seconds sounds once.  When
// there is no player or no file, fallback() runs instead (a bell).
namespace Sound
{
	void PlayNotification(std::function<void()> fallback);
}
