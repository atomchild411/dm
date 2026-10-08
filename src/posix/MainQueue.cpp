#include "MainQueue.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
	struct Item
	{
		std::function<void()> fn;
		bool* done; // set (under the lock) once fn ran, for Send
	};

	std::mutex g_lock;
	std::condition_variable g_doneCond;
	std::condition_variable g_workCond; // for Wait
	std::deque<Item> g_items;
	std::thread::id g_mainThread;
	int g_pipe[2] = { -1, -1 };
	bool g_shutdown = false;

	std::function<void()> g_wakeHook;

	void Wake()
	{
#ifndef _WIN32
		char c = 0;
		while (write(g_pipe[1], &c, 1) < 0 && errno == EINTR)
			;
#endif
		g_workCond.notify_all();
		if (g_wakeHook)
			g_wakeHook();
	}
}

void MainQueue::Init()
{
	g_mainThread = std::this_thread::get_id();
#ifndef _WIN32
	// (Windows has no pipe that select() or a toolkit could watch: its
	// loops wake through the hook, or sleep in Wait)
	if (pipe(g_pipe) == 0) {
		fcntl(g_pipe[0], F_SETFL, fcntl(g_pipe[0], F_GETFL) | O_NONBLOCK);
		fcntl(g_pipe[1], F_SETFL, fcntl(g_pipe[1], F_GETFL) | O_NONBLOCK);
		fcntl(g_pipe[0], F_SETFD, FD_CLOEXEC);
		fcntl(g_pipe[1], F_SETFD, FD_CLOEXEC);
	}
#endif
}

int MainQueue::WakeFd()
{
	return g_pipe[0];
}

void MainQueue::SetWakeHook(std::function<void()> fn)
{
	g_wakeHook = fn;
}

bool MainQueue::OnMainThread()
{
	return std::this_thread::get_id() == g_mainThread;
}

void MainQueue::Post(std::function<void()> fn)
{
	{
		std::lock_guard<std::mutex> lk(g_lock);
		if (g_shutdown)
			return;
		g_items.push_back(Item{ std::move(fn), nullptr });
	}
	Wake();
}

void MainQueue::Send(std::function<void()> fn)
{
	if (OnMainThread()) {
		fn();
		return;
	}

	bool done = false;
	std::unique_lock<std::mutex> lk(g_lock);
	if (g_shutdown)
		return;
	g_items.push_back(Item{ std::move(fn), &done });
	Wake();
	g_doneCond.wait(lk, [&] { return done || g_shutdown; });
}

void MainQueue::Wait(int ms)
{
	std::unique_lock<std::mutex> lk(g_lock);
	g_workCond.wait_for(lk, std::chrono::milliseconds(ms), [] { return !g_items.empty() || g_shutdown; });
}

void MainQueue::Drain()
{
#ifndef _WIN32
	char buf[64];
	while (read(g_pipe[0], buf, sizeof buf) > 0)
		;
#endif

	for (;;)
	{
		Item item;
		{
			std::lock_guard<std::mutex> lk(g_lock);
			if (g_items.empty())
				return;
			item = std::move(g_items.front());
			g_items.pop_front();
		}

		item.fn();

		if (item.done) {
			std::lock_guard<std::mutex> lk(g_lock);
			*item.done = true;
			g_doneCond.notify_all();
		}
	}
}

void MainQueue::Shutdown()
{
	std::lock_guard<std::mutex> lk(g_lock);
	g_shutdown = true;
	g_items.clear();
	g_doneCond.notify_all();
}
