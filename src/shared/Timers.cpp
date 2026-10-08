#include "Timers.hpp"

#include <map>

namespace
{
	Timers::Backend g_backend;
	std::map<int, void*> g_pending; // id -> the backend's handle
	int g_nextId = 1;
}

void Timers::SetBackend(const Backend& backend)
{
	g_backend = backend;
}

int Timers::After(int ms, std::function<void()> fn)
{
	int id = g_nextId++;
	if (g_nextId <= 0)
		g_nextId = 1;
	g_pending[id] = g_backend.add(ms, [id, fn] {
		g_pending.erase(id);
		fn();
	});
	return id;
}

void Timers::Cancel(int id)
{
	auto it = g_pending.find(id);
	if (it == g_pending.end())
		return;
	g_backend.remove(it->second);
	g_pending.erase(it);
}
