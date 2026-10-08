#pragma once

#include <functional>

// Work handed to the UI thread from the network threads.  The UI thread
// watches WakeFd() (with XtAppAddInput, select, ...) and calls Drain() when
// it becomes readable.
namespace MainQueue
{
	// Call once, on the UI thread.
	void Init();

	// Readable while work is queued (-1 on Windows: use Wait or the hook).
	int WakeFd();

	// Sleeps until work is queued or ms milliseconds passed, for loops that
	// have nothing else to sleep on.
	void Wait(int ms);

	// Also called (on the posting thread) whenever work is queued, for UI
	// loops that sleep in their toolkit rather than on WakeFd (GLFW's
	// glfwPostEmptyEvent).  Set before other threads start.
	void SetWakeHook(std::function<void()> fn);

	// Runs fn later on the UI thread.
	void Post(std::function<void()> fn);

	// Runs fn on the UI thread and waits for it.  On the UI thread itself
	// fn runs at once.
	void Send(std::function<void()> fn);

	// Runs everything queued so far.  UI thread only.
	void Drain();

	// After this, Send and Post drop their work, so threads still running
	// while the program exits never wait for a UI thread that is gone.
	void Shutdown();

	bool OnMainThread();
}
