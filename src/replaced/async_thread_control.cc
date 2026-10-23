// Meta4MPCXXL replacement for MetaModule's simulator/stubs/async_thread_control.cc (GPL-3.0, 4ms Company).
//
// Same API (coreproc_plugin/async_thread_control.hh): modules get a background task that a worker thread runs
// every millisecond (file streaming, loading samples). Changes from the 4ms stub, because inside MPC several
// plugin instances load and unload patches on their own threads while the runner works:
//  - tasks live in fixed slots that never move, so the Task* a module holds stays valid (the stub erased from a
//    vector, shifting the other tasks under their owners);
//  - creating and destroying a task takes a lock, and destroying waits until the runner is not inside that task;
//  - the runner is reference counted: start/kill pair up per plugin instance, the thread ends with the last one
//    (MPC may unload the .so after that, so no thread may outlive it).
#include "coreproc_plugin/async_thread_control.hh"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <thread>

namespace MetaModule
{

namespace
{

constexpr unsigned MaxTasks = 512;

struct Slot {
	Async::Task task{};
	std::atomic<bool> used{false};
};

std::array<Slot, MaxTasks> slots;
std::mutex slots_lock;		   // create/destroy
std::mutex runner_lock;		   // start/kill refcount
std::atomic<int> running_slot{-1};
std::atomic<bool> kill_signal{false};
std::atomic<int> pause_count{0};
std::atomic<uint32_t> next_id{1};
int refcount = 0;
std::thread runner;

int find_slot(uint32_t id) {
	for (unsigned i = 0; i < MaxTasks; i++)
		if (slots[i].used.load(std::memory_order_acquire) && slots[i].task.id == id)
			return (int)i;
	return -1;
}

void run() {
	while (!kill_signal.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (pause_count.load() > 0)
			continue;
		for (unsigned i = 0; i < MaxTasks; i++) {
			auto &s = slots[i];
			if (!s.used.load(std::memory_order_acquire))
				continue;
			running_slot.store((int)i);
			// re-check under the "running" mark: destroy_async_task() waits for running_slot to move on
			if (s.used.load(std::memory_order_acquire) && s.task.enabled && s.task.action) {
				s.task.action();
				if (s.task.one_shot)
					s.task.enabled = false;
			}
			running_slot.store(-1);
		}
	}
}

} // namespace

std::optional<uint32_t> create_async_task(CoreProcessor *module) {
	std::lock_guard<std::mutex> g{slots_lock};
	for (auto &s : slots) {
		if (!s.used.load()) {
			s.task = Async::Task{};
			s.task.id = next_id.fetch_add(1);
			s.task.module = module;
			s.task.enabled = false;
			s.used.store(true, std::memory_order_release);
			return s.task.id;
		}
	}
	return std::nullopt;
}

void destroy_async_task(uint32_t id) {
	std::lock_guard<std::mutex> g{slots_lock};
	int i = find_slot(id);
	if (i < 0)
		return;
	slots[i].task.enabled = false;
	slots[i].used.store(false, std::memory_order_release);
	while (running_slot.load() == i)
		std::this_thread::sleep_for(std::chrono::microseconds(100));
	slots[i].task = Async::Task{};
}

void start_module_threads() {
	std::lock_guard<std::mutex> g{runner_lock};
	if (refcount++ == 0) {
		kill_signal.store(false);
		runner = std::thread(run);
	}
}

void kill_module_threads() {
	std::lock_guard<std::mutex> g{runner_lock};
	if (refcount > 0 && --refcount == 0) {
		kill_signal.store(true);
		if (runner.joinable())
			runner.join();
	}
}

void pause_module_threads() {
	pause_count.fetch_add(1);
}
void pause_module_threads(unsigned) {
	pause_module_threads();
}
void resume_module_threads() {
	if (pause_count.load() > 0)
		pause_count.fetch_sub(1);
}
void resume_module_threads(unsigned) {
	resume_module_threads();
}

void peg_task_to_core(uint32_t, uint32_t) {
}

bool is_any_thread_executing() {
	return running_slot.load() >= 0;
}

Async::Task *get_task(unsigned id) {
	int i = find_slot(id);
	return i < 0 ? nullptr : &slots[i].task;
}

} // namespace MetaModule
