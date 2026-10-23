#include "cv_port.hh"
#include <chrono>
#include <cmath>
#include <cstring>
#include <dlfcn.h>

namespace Meta4MPC
{

namespace
{

// The few ALSA sequencer definitions used here (alsa/seq_event.h, alsa/seq.h; a stable ABI), so the plugin needs no
// ALSA headers or library at build time.
struct seq_addr {
	unsigned char client, port;
};
struct seq_event {
	unsigned char type, flags, tag, queue;
	unsigned int time[2];
	seq_addr source, dest;
	union {
		struct {
			unsigned char channel, note, velocity, off_velocity;
			unsigned int duration;
		} note;
		struct {
			unsigned char channel, unused1, unused2, unused3;
			unsigned int param;
			int value;
		} control;
		unsigned char raw8[12];
	} data;
};
static_assert(sizeof(seq_event) == 28, "snd_seq_event_t is 28 bytes");

constexpr unsigned char EV_NOTEON = 6, EV_NOTEOFF = 7, EV_CONTROLLER = 10;
constexpr unsigned char QUEUE_DIRECT = 253, ADDR_SUBSCRIBERS = 254, ADDR_UNKNOWN = 253;
constexpr int OPEN_OUTPUT = 1, NONBLOCK = 1;
constexpr unsigned CAP_READ = 1u << 0, CAP_SUBS_READ = 1u << 5;
constexpr unsigned TYPE_MIDI_GENERIC = 1u << 1, TYPE_APPLICATION = 1u << 20;

using fn_open = int (*)(void **, const char *, int, int);
using fn_close = int (*)(void *);
using fn_set_name = int (*)(void *, const char *);
using fn_port = int (*)(void *, const char *, unsigned, unsigned);
using fn_output = int (*)(void *, seq_event *);

struct Api {
	fn_open open = nullptr;
	fn_close close = nullptr;
	fn_set_name set_name = nullptr;
	fn_port port = nullptr;
	fn_output output = nullptr;
};

Api api_of(void *lib) {
	Api a;
	a.open = (fn_open)dlsym(lib, "snd_seq_open");
	a.close = (fn_close)dlsym(lib, "snd_seq_close");
	a.set_name = (fn_set_name)dlsym(lib, "snd_seq_set_client_name");
	a.port = (fn_port)dlsym(lib, "snd_seq_create_simple_port");
	a.output = (fn_output)dlsym(lib, "snd_seq_event_output_direct");
	return a;
}

constexpr unsigned CcFirst = 16;
constexpr unsigned CcMinFrames = 220; // at most one CC per output every 5 ms

} // namespace

CvPort::CvPort(std::string client_name)
	: name_{std::move(client_name)} {
	thr_ = std::thread([this] { run(); });
}

CvPort::~CvPort() {
	quit_.store(true);
	if (thr_.joinable())
		thr_.join();
	if (seq_) {
		auto a = api_of(lib_);
		if (a.close)
			a.close(seq_);
	}
	if (lib_)
		dlclose(lib_);
}

void CvPort::push(uint8_t a, uint8_t b, uint8_t c) {
	unsigned h = head_.load(std::memory_order_relaxed);
	unsigned next = (h + 1) % QSize;
	if (next == tail_.load(std::memory_order_acquire))
		return; // full: drop
	q_[h] = Msg{{a, b, c}};
	head_.store(next, std::memory_order_release);
}

void CvPort::set_mode(int m) {
	if (m < Off || m > CCOnly)
		m = Off;
	if (m != mode_.load()) {
		all_off();
		mode_.store(m);
	}
}

void CvPort::set_channel(int ch) {
	ch = ch < 1 ? 1 : ch > 16 ? 16 : ch;
	if (ch - 1 != chan_) {
		all_off();
		chan_ = ch - 1;
	}
}

void CvPort::all_off() {
	if (note_ >= 0)
		push(0x80 | chan_, (uint8_t)note_, 0);
	note_ = -1;
	gate_ = false;
	cc_last_.fill(-1);
}

void CvPort::sample(const float *v, unsigned frames) {
	int m = mode_.load(std::memory_order_relaxed);
	if (m == Off)
		return;
	unsigned first_cc = 0;
	if (m == NotesCC) {
		bool g = v[1] > 1.f;
		int note = (int)std::lround(60.f + 12.f * v[0]);
		note = note < 0 ? 0 : note > 127 ? 127 : note;
		if (g && !gate_) {
			push(0x90 | chan_, (uint8_t)note, 100);
			note_ = note;
		} else if (g && gate_ && note != note_) {
			// legato: the new pitch first, then release the old one, so the gate stays high
			push(0x90 | chan_, (uint8_t)note, 100);
			if (note_ >= 0)
				push(0x80 | chan_, (uint8_t)note_, 0);
			note_ = note;
		} else if (!g && gate_) {
			if (note_ >= 0)
				push(0x80 | chan_, (uint8_t)note_, 0);
			note_ = -1;
		}
		gate_ = g;
		first_cc = 2;
	}
	for (unsigned i = first_cc; i < 6; i++) {
		unsigned slot = i - first_cc;
		cc_wait_[slot] = cc_wait_[slot] > frames ? cc_wait_[slot] - frames : 0;
		if (cc_wait_[slot])
			continue;
		int val = (int)std::lround(v[i] * 12.7f);
		val = val < 0 ? 0 : val > 127 ? 127 : val;
		if (val != cc_last_[slot]) {
			push(0xB0 | chan_, (uint8_t)(CcFirst + slot), (uint8_t)val);
			cc_last_[slot] = val;
			cc_wait_[slot] = CcMinFrames;
		}
	}
}

bool CvPort::open_port() {
	if (port_ >= 0)
		return true;
	if (!lib_)
		lib_ = dlopen("libasound.so.2", RTLD_NOW | RTLD_LOCAL);
	if (!lib_)
		return false;
	auto a = api_of(lib_);
	if (!a.open || !a.port || !a.output)
		return false;
	if (!seq_ && a.open(&seq_, "default", OPEN_OUTPUT, NONBLOCK) < 0) {
		seq_ = nullptr;
		return false;
	}
	if (a.set_name)
		a.set_name(seq_, name_.c_str());
	port_ = a.port(seq_, "CV Out", CAP_READ | CAP_SUBS_READ, TYPE_MIDI_GENERIC | TYPE_APPLICATION);
	return port_ >= 0;
}

void CvPort::send(Msg const &m) {
	auto a = api_of(lib_);
	seq_event ev{};
	ev.queue = QUEUE_DIRECT;
	ev.source.port = (unsigned char)port_;
	ev.dest.client = ADDR_SUBSCRIBERS;
	ev.dest.port = ADDR_UNKNOWN;
	uint8_t st = m.b[0] & 0xF0, ch = m.b[0] & 0x0F;
	if (st == 0x90) {
		ev.type = EV_NOTEON;
		ev.data.note = {ch, m.b[1], m.b[2], 0, 0};
	} else if (st == 0x80) {
		ev.type = EV_NOTEOFF;
		ev.data.note = {ch, m.b[1], 0, 0, 0};
	} else if (st == 0xB0) {
		ev.type = EV_CONTROLLER;
		ev.data.control.channel = ch;
		ev.data.control.param = m.b[1];
		ev.data.control.value = m.b[2];
	} else {
		return;
	}
	a.output(seq_, &ev);
}

void CvPort::run() {
	unsigned retry = 0;
	while (!quit_.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (mode_.load() == Off && head_.load() == tail_.load())
			continue;
		if (port_ < 0) {
			if (retry) {
				retry--;
				tail_.store(head_.load()); // nowhere to send: drop
				continue;
			}
			if (!open_port()) {
				retry = 2000; // try again in ~2 s
				continue;
			}
		}
		unsigned t = tail_.load(std::memory_order_relaxed);
		while (t != head_.load(std::memory_order_acquire)) {
			send(q_[t]);
			t = (t + 1) % QSize;
			tail_.store(t, std::memory_order_release);
		}
	}
}

} // namespace Meta4MPC
