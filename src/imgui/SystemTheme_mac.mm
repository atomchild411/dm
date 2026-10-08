// SystemTheme on macOS: the system's appearance, and the app's to match.

#include "SystemTheme.hpp"

#import <Cocoa/Cocoa.h>

void SystemTheme::Query(std::function<void(bool dark)> answer)
{
	// the system's own choice (the app's effectiveAppearance would be the
	// override, when there is one)
	NSString* style = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
	if (answer)
		answer(style && [style caseInsensitiveCompare:@"Dark"] == NSOrderedSame);
}

bool SystemTheme::CheapToPoll()
{
	return true;
}

void SystemTheme::FrameWindows(GLFWwindow*, bool dark, bool followSystem)
{
	// the app's appearance: title bars, the login windows, menus
	NSApp.appearance = followSystem ? nil :
		[NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
}

void SystemTheme::FrameNativeWindow(void*)
{
	// (the app's appearance covers every window)
}
