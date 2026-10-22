// Meta4msXXL: plays 4ms MetaModule patches (VCV Rack patches exported with the MetaModule hub) as an MPC OS plugin.
//
// The patch player is 4ms's own (firmware/src/patch_play, vendored in third_party/metamodule), the code their
// headless simulator runs. This file is the MPC side: the wrapper's engine contract (mpc-vst-plugins wrapper/engine.h),
// one worker thread per instance for scanning and loading patches, MIDI from the track into the patch, the host's
// tempo as MIDI clock, the Q-Link pages (pages.cc) and the CV port (cv_port.cc).
//
// Built twice: Meta4msXXL (instrument) and, with -DMETA4MS_FX, Meta4msXXL FX (effect: the track's audio goes into the
// patch's In 1 / In 2).
extern "C" {
#include "engine.h"
}
#include "cv_port.hh"
#include "library.hh"
#include "pages.hh"
#include "wifi.hh"

#include "CoreModules/moduleFactory.hh"
#include "coreproc_plugin/async_thread_control.hh"
#include "core_m4/midi_controls.hh"
#include "load_internal_plugins.hh"
#include "midi/midi_router.hh"
#include "patch-serial/yaml_to_patch.hh"
#include "patch_play/patch_player.hh"
#include "plugin/Plugin.hpp"
// must come after plugin/Plugin.hpp:
#include "ext_plugin_builtin.hh"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

using namespace MetaModule;

namespace Meta4ms
{

#ifdef META4MS_FX
constexpr bool IsFx = true;
constexpr const char *PlugName = "Meta4msXXL FX";
#else
constexpr bool IsFx = false;
constexpr const char *PlugName = "Meta4msXXL";
#endif

constexpr float SampleRate = 44100.f;
constexpr float VoltsFullScale = 10.24f; // MetaModule's codec: about +/-10.24 V is full scale (calibration_data.hh)
constexpr unsigned NumTabs = 5;
constexpr unsigned CvEvery = 16; // sample the CV outputs every 16 frames (0.36 ms)

// ---------------------------------------------------------------- once per process

std::once_flag plugins_once;

// Registered once and never freed: the module registry is a static of its own, and destroying the plugins at exit
// (or when MPC unloads the .so) after the registry is gone crashed in Plugin::~Plugin -> unregisterModule.
std::list<rack::plugin::Plugin> *builtin_plugins = nullptr;

void register_modules() {
	std::call_once(plugins_once, [] {
		builtin_plugins = new std::list<rack::plugin::Plugin>;
		load_internal_plugins(*builtin_plugins);
		load_ext_builtin_plugins(*builtin_plugins);
	});
}

// ---------------------------------------------------------------- a loaded patch

struct Loaded {
	std::unique_ptr<PatchPlayer> player;
	PatchData pd;
	Pages pages;
	std::string label, rel, note;
	int index = -1;
	bool forced = false; // loaded by a state restore or a Wi-Fi upload, not by the host's "patch" value
	bool out1 = false, out2 = false;
	unsigned modules = 0, missing = 0;
	std::vector<StaticParam> file_values; // the patch file's own values, to save only what was changed

	~Loaded() {
		if (player)
			player->unload_patch();
	}
};

struct Restore {
	std::string rel, label;
	int page = 0;
	std::vector<StaticParam> params;
	std::vector<std::pair<std::pair<unsigned, unsigned>, float>> knobs; // (set, panel knob) -> position
	bool active = false;
};

// ---------------------------------------------------------------- the instance

struct Inst {
	std::string factory_dir;

	// worker
	std::thread worker;
	std::mutex wmx;
	std::condition_variable wcv;
	bool quit = false;
	bool want_scan = true;
	int want_index = -1;   // patch to load (library index)
	bool force_next = false; // the next load comes from a restore or an upload
	std::string want_path; // or: load this file once the library has been rescanned (a Wi-Fi upload)
	Restore restore;	   // applied to the next load of that patch
	std::vector<PatchEntry> library;

	// hand-over to the audio side
	std::mutex swap_mx;
	std::unique_ptr<Loaded> ready; // loaded, waiting for the audio thread
	std::unique_ptr<Loaded> trash; // replaced, for the worker to free
	std::atomic<int> lib_size{0};
	std::atomic<bool> loading{false};
	std::atomic<int> loading_index{-1};
	std::string lib_label_cache; // label of loading_index, under wmx

	// audio side (all calls below are serialised by the wrapper's per-instance lock)
	std::unique_ptr<Loaded> cur;
	unsigned page = 0;
	float level_db = 0.f;
	Midi::MessageParser parser;
	CvPort cv;
	unsigned cv_count = 0;
	uint32_t changed = 0; // bits: 0-7 k1..k8, 8 page, 9 patch
	float cpu = 0.f;
	int requested = -1; // last patch index the host asked for

	// MIDI -> the hub's inputs, for patches made to be played from CV/gates (parameter "midi_ins")
	//   Keys: Gate In 1 = gate while any note is held, Gate In 2 = a 5 ms trigger on every note-on,
	//         In 3 = pitch of the last note (1 V/oct, 0 V = C4), In 4 = its velocity (0..10 V)
	//   Pads: notes 36-43 (the Force's first pad row) gate Gate In 1, Gate In 2, In 3, In 4, In 5, In 6,
	//         then In 1, In 2 (instrument only: on the FX, In 1/In 2 carry the track's audio)
	enum { MidiInsOff = 0, MidiInsKeys = 1, MidiInsPads = 2 };
	int midi_ins = MidiInsKeys;
	float jack_v[8] = {};
	bool jack_dirty[8] = {};
	unsigned trig_frames = 0;
	unsigned held = 0;
	static constexpr float GateV = 8.f; // MetaModule's gate inputs read high at 8 V

	// transport -> MIDI clock
	bool playing = false;
	double next_pulse = -1;
	std::vector<unsigned> pulses; // frame offsets of clock pulses in the coming block
	bool send_start = false, send_stop = false;

	WifiReceiver wifi{[this](std::string const &path) {
		{
			std::lock_guard<std::mutex> g{wmx};
			want_scan = true;
			want_path = path;
		}
		wcv.notify_all();
	}};

	explicit Inst(const char *data_dir)
		: cv{instance_name()} {
		if (data_dir)
			factory_dir = data_dir;
		register_modules();
		start_module_threads();
		pulses.reserve(64);
		worker = std::thread([this] { run_worker(); });
	}

	~Inst() {
		{
			std::lock_guard<std::mutex> g{wmx};
			quit = true;
		}
		wcv.notify_all();
		if (worker.joinable())
			worker.join();
		cur.reset();
		ready.reset();
		trash.reset();
		kill_module_threads();
	}

	static std::string instance_name() {
		static std::atomic<int> n{0};
		int k = ++n;
		return k == 1 ? std::string{PlugName} : std::string{PlugName} + " " + std::to_string(k);
	}

	// ------------------------------------------------ worker thread

	std::unique_ptr<Loaded> load(PatchEntry const &e) {
		auto yaml = read_file(e.path);
		auto L = std::make_unique<Loaded>();
		L->label = e.label;
		L->rel = e.rel;
		if (yaml.empty()) {
			L->note = "CANNOT READ FILE";
			return L;
		}
		std::vector<char> buf(yaml.begin(), yaml.end());
		buf.push_back('\0');
		if (!yaml_raw_to_patch(std::span<char>{buf.data(), yaml.size()}, L->pd) || L->pd.module_slugs.empty()) {
			L->note = "NOT A METAMODULE PATCH";
			return L;
		}
		if (L->pd.patch_name.length() == 0)
			L->pd.patch_name.copy(e.label);

		auto player = std::make_unique<PatchPlayer>();
		player->set_samplerate(SampleRate);
		auto res = player->load_patch(L->pd);
		if (!res.success) {
			L->note = res.error_string;
			return L;
		}
		for (unsigned i = 0; i < 8; i++)
			player->set_input_jack_patched_status(i, IsFx && i < 2);
		for (unsigned i = 0; i < 8; i++)
			player->set_output_jack_patched_status(i, true);
		player->set_midi_connected();

		for (auto &m : L->pd.mapped_outs) {
			if (m.panel_jack_id == 0)
				L->out1 = true;
			if (m.panel_jack_id == 1)
				L->out2 = true;
		}
		L->modules = (unsigned)L->pd.module_slugs.size() - 1;
		for (unsigned i = 1; i < L->pd.module_slugs.size(); i++)
			if (!ModuleFactory::isValidSlug(L->pd.module_slugs[i]))
				L->missing++;
		L->file_values = L->pd.static_knobs;
		L->player = std::move(player);
		L->pages.build(L->pd, *L->player);
		return L;
	}

	void apply_restore(Loaded &L, Restore const &r) {
		for (auto &p : r.params)
			L.player->apply_static_param(p);
		for (auto &[k, pos] : r.knobs) {
			for (auto &pg : L.pages.pages)
				for (auto &it : pg.items)
					if (it.kind == Item::Kind::KnobSetKnob && it.knob_set == k.first && it.panel_knob == k.second)
						Pages::set(it, pos, *L.player);
		}
	}

	void run_worker() {
		std::unique_lock<std::mutex> lk{wmx};
		while (!quit) {
			wcv.wait_for(lk, std::chrono::milliseconds(20));
			if (quit)
				break;

			// free what the audio thread swapped out
			{
				std::unique_ptr<Loaded> t;
				{
					std::lock_guard<std::mutex> g{swap_mx};
					t = std::move(trash);
				}
				if (t) {
					lk.unlock();
					t.reset();
					lk.lock();
				}
			}

			if (want_scan) {
				want_scan = false;
				lk.unlock();
				auto lib = scan_library(factory_dir);
				lk.lock();
				library = std::move(lib);
				lib_size.store((int)library.size());
				if (want_index < 0 && !restore.active && !cur_known())
					want_index = default_index();
				if (restore.active) {
					int i = find_entry(library, restore.rel, restore.label);
					want_index = i >= 0 ? i : default_index();
				}
				if (!want_path.empty()) {
					for (size_t i = 0; i < library.size(); i++)
						if (library[i].path == want_path)
							want_index = (int)i;
					want_path.clear();
					restore.active = false;
					force_next = true;
				}
			}

			if (want_index >= (int)library.size() && !library.empty())
				want_index = (int)library.size() - 1;
			if (want_index >= 0 && want_index < (int)library.size()) {
				int idx = want_index;
				want_index = -1;
				auto entry = library[idx];
				bool use_restore = restore.active && (entry.rel == restore.rel || entry.label == restore.label);
				Restore r = use_restore ? restore : Restore{};
				if (use_restore)
					restore.active = false;
				loading_index.store(idx);
				lib_label_cache = entry.label;
				loading.store(true);
				lk.unlock();
				bool forced = use_restore || force_next;
				force_next = false;
				auto L = load(entry);
				L->index = idx;
				L->forced = forced;
				if (L->player && use_restore)
					apply_restore(*L, r);
				if (use_restore)
					pending_page.store(r.page);
				{
					std::lock_guard<std::mutex> g{swap_mx};
					ready = std::move(L); // a load that was never picked up is dropped here
				}
				lk.lock();
				loading.store(false);
			}
		}
	}

	std::atomic<int> pending_page{-1};
	std::atomic<bool> has_cur{false};

	bool cur_known() const {
		return has_cur.load();
	}

	// the patch a new instance starts with: one that plays from pads and keys (Keys mode) on the instrument
	int default_index() const {
		for (size_t i = 0; i < library.size(); i++)
			if (library[i].factory && library[i].label == (IsFx ? "DJEMBE4VERB" : "ENOSCPOLY4"))
				return (int)i;
		return library.empty() ? -1 : 0;
	}

	void request_index(int i) {
		{
			std::lock_guard<std::mutex> g{wmx};
			want_index = i;
		}
		wcv.notify_all();
	}

	// ------------------------------------------------ audio side

	void take_ready() {
		std::unique_lock<std::mutex> g{swap_mx, std::try_to_lock};
		if (!g.owns_lock() || !ready)
			return;
		std::swap(cur, ready);
		trash = std::move(ready); // the old patch (or nothing); the worker frees it
		has_cur.store(true);
		if (cur && cur->forced)
			requested = cur->index; // the host's "patch" value follows a load it did not ask for
		parser = Midi::MessageParser{};
		if (cur && cur->player)
			parser.set_poly_num(std::max<uint32_t>(1, cur->player->get_midi_poly_num()));
		cv.all_off();
		mark_midi_jacks();
		int pp = pending_page.exchange(-1);
		page = 0;
		if (pp >= 0 && cur && (unsigned)pp < cur->pages.pages.size())
			page = (unsigned)pp;
		changed |= 0x3FF;
	}

	Page *cur_page() {
		if (!cur || !cur->player || cur->pages.pages.empty())
			return nullptr;
		if (page >= cur->pages.pages.size())
			page = (unsigned)cur->pages.pages.size() - 1;
		return &cur->pages.pages[page];
	}

	Item *slot(unsigned k) {
		auto *pg = cur_page();
		if (!pg || k >= pg->items.size())
			return nullptr;
		return &pg->items[k];
	}

	unsigned page_count() const {
		return cur && cur->player ? (unsigned)cur->pages.pages.size() : 0;
	}

	unsigned tab_start() const {
		unsigned n = page_count();
		if (n <= NumTabs)
			return 0;
		unsigned s = page > 2 ? page - 2 : 0;
		return std::min(s, n - NumTabs);
	}

	void set_page(int p) {
		unsigned n = page_count();
		if (!n) {
			page = (unsigned)std::clamp(p, 0, 63); // no patch yet: keep the value (read back by the host)
			return;
		}
		p = std::clamp(p, 0, (int)n - 1);
		if ((unsigned)p != page) {
			page = (unsigned)p;
			changed |= 0x1FF;
		}
	}

	void set_jack(unsigned j, float v) {
		if (j < 8 && jack_v[j] != v) {
			jack_v[j] = v;
			jack_dirty[j] = true;
		}
	}

	void midi_to_jacks(const uint8_t *m, int len) {
		if (midi_ins == MidiInsOff || len < 3)
			return;
		uint8_t st = m[0] & 0xF0;
		bool on = st == 0x90 && m[2] > 0, off = st == 0x80 || (st == 0x90 && m[2] == 0);
		if (!on && !off)
			return;
		if (midi_ins == MidiInsKeys) {
			if (on) {
				held++;
				set_jack(6, GateV);
				set_jack(7, GateV);
				trig_frames = 221; // 5 ms
				set_jack(2, (m[1] - 60) / 12.f);
				set_jack(3, m[2] / 12.7f);
			} else {
				held = held ? held - 1 : 0;
				if (!held)
					set_jack(6, 0);
			}
			return;
		}
		static const int8_t pad_jack[8] = {6, 7, 2, 3, 4, 5, 0, 1};
		int n = m[1] - 36;
		if (n < 0 || n > 7 || (IsFx && n > 5))
			return;
		set_jack((unsigned)pad_jack[n], on ? GateV : 0.f);
	}

	void mark_midi_jacks() {
		if (!cur || !cur->player)
			return;
		for (unsigned j = 0; j < 8; j++) {
			bool audio = IsFx && j < 2;
			bool used = midi_ins == MidiInsKeys ? (j == 2 || j == 3 || j == 6 || j == 7)
					  : midi_ins == MidiInsPads ? (!IsFx || j >= 2)
												: false;
			cur->player->set_input_jack_patched_status(j, audio || used);
			if (!audio) {
				jack_v[j] = 0;
				jack_dirty[j] = true;
			}
		}
		held = 0;
		trig_frames = 0;
	}

	void midi_in(const uint8_t *m, int len) {
		if (len < 1 || !cur || !cur->player)
			return;
		midi_to_jacks(m, len);
		auto &player = *cur->player;
		MidiMessage msg{m[0], (uint8_t)(len > 1 ? m[1] : 0), (uint8_t)(len > 2 ? m[2] : 0)};
		if (msg.status != 0xfe && !msg.is_sysex())
			MidiRouter::push_incoming_message(msg, Midi::USB);
		auto event = parser.parse(msg);
		event.port = Midi::USB;
		using T = Midi::Event::Type;
		unsigned poly_num = std::max<uint32_t>(1, player.get_midi_poly_num());
		switch (event.type) {
			case T::NoteOn:
				player.set_midi_note_pitch(event.poly_chan, Midi::note_to_volts(event.note), event.midi_chan, event.port);
				player.set_midi_note_gate(event.poly_chan, 10.f, event.midi_chan, event.port);
				player.set_midi_note_velocity(event.poly_chan, event.val, event.midi_chan, event.port);
				player.set_midi_note_retrig(event.poly_chan, 10.f, event.midi_chan, event.port);
				player.set_midi_gate(event.note, 10.f, event.midi_chan, event.port);
				break;
			case T::NoteOff:
				if (event.poly_chan < poly_num)
					player.set_midi_note_gate(event.poly_chan, 0, event.midi_chan, event.port);
				player.set_midi_gate(event.note, 0, event.midi_chan, event.port);
				break;
			case T::Aft:
				player.set_midi_note_aftertouch(event.poly_chan, event.val, event.midi_chan, event.port);
				break;
			case T::ChanPress:
				for (unsigned i = 0; i < poly_num; i++)
					player.set_midi_note_aftertouch(i, event.val, event.midi_chan, event.port);
				break;
			case T::CC:
				player.set_midi_cc(event.note, event.val, event.midi_chan, event.port);
				break;
			case T::Bend:
				player.set_midi_cc(128, event.val, event.midi_chan, event.port);
				break;
			case T::Time:
				player.send_midi_time_event(event.note, 10.f, event.port);
				break;
			default:
				break;
		}
	}

	void song_pos(double ppq, double bpm, int is_playing, int frames) {
		pulses.clear();
		if (is_playing && !playing) {
			send_start = true;
			next_pulse = -1;
		}
		if (!is_playing && playing)
			send_stop = true;
		playing = is_playing;
		if (!is_playing || ppq < 0 || bpm <= 0)
			return;
		double beats = frames * bpm / 60.0 / SampleRate;
		double tick = 1.0 / 24.0;
		// first pulse at or after the block start; resync after a jump (loop, locate)
		double first = std::ceil(ppq / tick - 1e-9) * tick;
		if (next_pulse < 0 || std::fabs(next_pulse - first) > tick * 1.5)
			next_pulse = first;
		while (next_pulse < ppq + beats - 1e-12) {
			double off = (next_pulse - ppq) / beats * frames;
			pulses.push_back((unsigned)std::clamp(off, 0.0, (double)frames - 1));
			next_pulse += tick;
		}
	}

	void clock_event(uint8_t ev, uint8_t raw) {
		if (!cur || !cur->player)
			return;
		cur->player->send_midi_time_event(ev, 10.f, Midi::USB);
		MidiRouter::push_incoming_message(MidiMessage{raw}, Midi::USB);
	}

	void drain_patch_midi_out() {
		// MIDI the patch sends (MIDI output modules) goes out on the CV port when it is on, else it is dropped
		while (auto m = MidiRouter::pop_outgoing_message()) {
			(void)m;
		}
	}

	template<typename In>
	void run(In &&input, int16_t *out, int frames) {
		auto t0 = std::chrono::steady_clock::now();
		take_ready();
		if (!cur || !cur->player || !cur->player->is_loaded) {
			std::memset(out, 0, sizeof(int16_t) * 2 * frames);
			pulses.clear();
			return;
		}
		auto &player = *cur->player;
		const float gain = std::pow(10.f, level_db / 20.f) / VoltsFullScale * 32767.f;
		const bool mono = cur->out1 && !cur->out2;
		const bool mono_r = !cur->out1 && cur->out2;

		if (send_stop) {
			clock_event(TimingEvents::Stop, 0xFC);
			send_stop = false;
		}
		if (send_start) {
			clock_event(TimingEvents::Start, 0xFA);
			send_start = false;
		}
		size_t pi = 0;
		float cvv[6];
		for (int i = 0; i < frames; i++) {
			while (pi < pulses.size() && pulses[pi] == (unsigned)i) {
				clock_event(TimingEvents::Clock, 0xF8);
				pi++;
			}
			if constexpr (IsFx) {
				float l, r;
				input(i, l, r);
				player.set_panel_input(0, l * VoltsFullScale);
				player.set_panel_input(1, r * VoltsFullScale);
			}
			if (trig_frames && --trig_frames == 0)
				set_jack(7, 0);
			for (unsigned j = 0; j < 8; j++) {
				if (jack_dirty[j]) {
					jack_dirty[j] = false;
					if (!(IsFx && j < 2))
						player.set_panel_input(j, jack_v[j]);
				}
			}
			player.update_patch();
			float l = player.get_panel_output(0), r = player.get_panel_output(1);
			if (mono)
				r = l;
			else if (mono_r)
				l = r;
			l *= gain;
			r *= gain;
			if (!std::isfinite(l))
				l = 0;
			if (!std::isfinite(r))
				r = 0;
			out[2 * i] = (int16_t)std::clamp(l, -32768.f, 32767.f);
			out[2 * i + 1] = (int16_t)std::clamp(r, -32768.f, 32767.f);

			if (++cv_count >= CvEvery) {
				if (cv.mode() != CvPort::Off) {
					for (unsigned k = 0; k < 6; k++)
						cvv[k] = player.get_panel_output(2 + k);
					cv.sample(cvv, cv_count);
				}
				cv_count = 0;
			}
		}
		pulses.clear();
		drain_patch_midi_out();

		double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		float pct = (float)(used / (frames / (double)SampleRate) * 100.0);
		cpu += (pct - cpu) * 0.05f;
	}

	// ------------------------------------------------ parameters

	static int key_index(const char *key, const char *prefix) {
		size_t n = std::strlen(prefix);
		if (std::strncmp(key, prefix, n) != 0)
			return -1;
		const char *p = key + n;
		if (*p < '1' || *p > '9')
			return -1;
		int v = *p - '0';
		return v;
	}

	void set_param(const char *key, const char *val) {
		float v = (float)std::atof(val);
		if (!std::strcmp(key, "state")) {
			set_state(val);
			return;
		}
		if (!std::strcmp(key, "patch")) {
			// the host reads the value straight back (stepping, Q-Links): answer with the request, not the
			// load that is still running on the worker
			int i = (int)std::lround(v);
			int n = lib_size.load();
			i = std::clamp(i, 0, n > 0 ? n - 1 : 1999);
			int curi = cur ? cur->index : -1;
			requested = i;
			if (i != curi || !cur || !cur->player)
				request_index(i);
			return;
		}
		if (!std::strcmp(key, "rescan")) {
			if (v >= 0.5f) {
				{
					std::lock_guard<std::mutex> g{wmx};
					want_scan = true;
				}
				wcv.notify_all();
			}
			return;
		}
		if (!std::strcmp(key, "page")) {
			set_page((int)std::lround(v));
			return;
		}
		if (!std::strcmp(key, "level")) {
			level_db = std::clamp(v, -24.f, 12.f);
			return;
		}
		if (!std::strcmp(key, "cv_mode")) {
			cv.set_mode((int)std::lround(v));
			return;
		}
		if (!std::strcmp(key, "cv_chan")) {
			cv.set_channel((int)std::lround(v));
			return;
		}
		if (!std::strcmp(key, "panic")) {
			if (v >= 0.5f)
				panic();
			return;
		}
		if (!std::strcmp(key, "wifi")) {
			wifi.set_enabled(v >= 0.5f);
			return;
		}
		if (!std::strcmp(key, "midi_ins")) {
			int m = std::clamp((int)std::lround(v), 0, 2);
			if (m != midi_ins) {
				midi_ins = m;
				mark_midi_jacks();
			}
			return;
		}
		int k = key_index(key, "tab_");
		if (k >= 1 && k <= (int)NumTabs && key[5] == 0) {
			if (v >= 0.5f) // a tap sends 1, then the wrapper springs the row back to 0
				set_page((int)tab_start() + k - 1);
			return;
		}
		k = key_index(key, "k");
		if (k >= 1 && k <= 8 && key[2] == 0) {
			if (auto *it = slot(k - 1))
				Pages::set(*it, v, *cur->player);
			return;
		}
	}

	void panic() {
		if (!cur || !cur->player)
			return;
		for (unsigned ch = 0; ch < 16; ch++)
			for (unsigned n = 0; n < 128; n++) {
				uint8_t m[3] = {(uint8_t)(0x80 | ch), (uint8_t)n, 0};
				midi_in(m, 3);
			}
		cv.all_off();
	}

	std::string page_name() {
		if (auto *pg = cur_page())
			return pg->title;
		return "";
	}

	std::string patch_name() {
		if (loading.load()) {
			std::lock_guard<std::mutex> g{wmx};
			return "LOADING " + lib_label_cache;
		}
		if (lib_size.load() == 0 && !cur)
			return "NO PATCHES FOUND";
		if (!cur)
			return "";
		std::string n = cur->pd.patch_name.length() ? std::string{std::string_view{cur->pd.patch_name}} : cur->label;
		for (auto &c : n)
			c = (char)std::toupper((unsigned char)c);
		return n;
	}

	std::string status() {
		if (!cur)
			return "";
		if (!cur->player)
			return cur->note;
		char b[96];
		const char *cvs = cv.mode() == CvPort::Off ? "CV OFF" : cv.port_open() ? "CV PORT ON" : "CV PORT ...";
		if (cur->missing)
			std::snprintf(b, sizeof b, "%u MODULE%s MISSING   CPU %d%%   %s", cur->missing, cur->missing > 1 ? "S" : "",
						  (int)std::lround(cpu), cvs);
		else
			std::snprintf(b, sizeof b, "%u MODULES   CPU %d%%   %s", cur->modules, (int)std::lround(cpu), cvs);
		return b;
	}

	int out_str(char *buf, int len, std::string const &s) {
		if (len <= 0)
			return 0;
		int n = std::min<int>((int)s.size(), len - 1);
		std::memcpy(buf, s.data(), n);
		buf[n] = 0;
		return n > 0 ? n : 1; // > 0: answered (an empty text is still an answer)
	}

	int out_num(char *buf, int len, double v) {
		return std::max(1, std::snprintf(buf, len, "%.6g", v));
	}

	int get_param(const char *key, char *buf, int len) {
		if (!std::strcmp(key, "state"))
			return out_str(buf, len, get_state());
		if (!std::strcmp(key, "patch"))
			return out_num(buf, len, requested >= 0 ? requested : cur ? std::max(0, cur->index) : 0);
		if (!std::strcmp(key, "patch_name"))
			return out_str(buf, len, patch_name());
		if (!std::strcmp(key, "page"))
			return out_num(buf, len, page);
		if (!std::strcmp(key, "page_name"))
			return out_str(buf, len, page_name());
		if (!std::strcmp(key, "page_pos")) {
			unsigned n = page_count();
			return out_str(buf, len, n ? std::to_string(page + 1) + " / " + std::to_string(n) : std::string{"-"});
		}
		if (!std::strcmp(key, "status"))
			return out_str(buf, len, status());
		if (!std::strcmp(key, "level"))
			return out_num(buf, len, level_db);
		if (!std::strcmp(key, "cv_mode")) {
			static const char *names[] = {"Off", "Notes + CC", "CC only"};
			return out_str(buf, len, names[std::clamp(cv.mode(), 0, 2)]);
		}
		if (!std::strcmp(key, "cv_chan"))
			return out_num(buf, len, cv.channel());
		if (!std::strcmp(key, "wifi"))
			return out_str(buf, len, wifi.enabled() ? "On" : "Off");
		if (!std::strcmp(key, "midi_ins")) {
			static const char *names[] = {"Off", "Keys", "Pads"};
			return out_str(buf, len, names[std::clamp(midi_ins, 0, 2)]);
		}
		if (!std::strcmp(key, "wifi_addr")) {
			auto a = wifi.address();
			return out_str(buf, len, a.empty() ? std::string{"WI-FI RECEIVE IS OFF"} : a);
		}
		if (!std::strcmp(key, "wifi_msg"))
			return out_str(buf, len, wifi.last_message());
		if (!std::strcmp(key, "rescan") || !std::strcmp(key, "panic"))
			return out_num(buf, len, 0);

		int k = key_index(key, "tab_");
		if (k >= 1 && k <= (int)NumTabs) {
			unsigned p = tab_start() + (unsigned)k - 1;
			bool on = !std::strcmp(key + 5, "_on");
			if (on)
				return out_num(buf, len, p == page && p < page_count() ? 1 : 0);
			if (key[5] == 0)
				return out_str(buf, len, p < page_count() ? cur->pages.pages[p].title : "");
		}
		k = key_index(key, "k");
		if (k >= 1 && k <= 8) {
			const char *suffix = key + 2;
			Item *it = slot(k - 1);
			if (!*suffix)
				return out_num(buf, len, it ? Pages::get(*it, *cur->player) : 0.0);
			if (!std::strcmp(suffix, "_name"))
				return out_str(buf, len, it ? it->name : std::string{"-"});
			if (!std::strcmp(suffix, "_display"))
				return out_str(buf, len, it ? Pages::value_text(*it, *cur->player) : std::string{" "});
		}
		return 0;
	}

	int next_changed(char *key, int len) {
		static const char *names[] = {"k1", "k2", "k3", "k4", "k5", "k6", "k7", "k8", "page", "patch"};
		for (unsigned b = 0; b < 10; b++) {
			if (changed & (1u << b)) {
				changed &= ~(1u << b);
				std::snprintf(key, len, "%s", names[b]);
				return 1;
			}
		}
		return 0;
	}

	// ------------------------------------------------ state

	std::string get_state() {
		std::string s = "M4XXL 1\n";
		char b[128];
		std::snprintf(b, sizeof b, "level=%.3g\ncvm=%d\ncvc=%d\nwifi=%d\nmins=%d\npage=%u\n", level_db, cv.mode(),
					  cv.channel(), wifi.enabled() ? 1 : 0, midi_ins, page);
		s += b;
		if (!cur)
			return s;
		s += "rel=" + cur->rel + "\nlabel=" + cur->label + "\n";
		if (!cur->player)
			return s;
		auto &player = *cur->player;
		std::string ps = "p=", ks = "k=";
		for (auto &pg : cur->pages.pages) {
			for (auto &it : pg.items) {
				if (it.kind == Item::Kind::KnobSetKnob) {
					std::snprintf(b, sizeof b, "%u.%u:%.5g,", it.knob_set, it.panel_knob, it.knob_pos);
					if (ks.size() + std::strlen(b) < 2000)
						ks += b;
					continue;
				}
				float v = player.get_param(it.module_id, it.param_id);
				float fv = -1.f;
				for (auto &f : cur->file_values)
					if (f.module_id == it.module_id && f.param_id == it.param_id)
						fv = f.value;
				if (std::fabs(v - fv) < 1e-4f)
					continue;
				std::snprintf(b, sizeof b, "%u.%u:%.5g,", it.module_id, it.param_id, v);
				if (ps.size() + std::strlen(b) < 5500)
					ps += b;
			}
		}
		s += ps + "\n" + ks + "\n";
		return s;
	}

	void set_state(const char *text) {
		if (std::strncmp(text, "M4XXL 1", 7) != 0)
			return;
		Restore r;
		r.active = true;
		const char *p = text;
		while (*p) {
			const char *e = std::strchr(p, '\n');
			std::string line = e ? std::string(p, e) : std::string(p);
			p = e ? e + 1 : p + std::strlen(p);
			auto eq = line.find('=');
			if (eq == std::string::npos)
				continue;
			std::string k = line.substr(0, eq), v = line.substr(eq + 1);
			if (k == "level")
				level_db = std::clamp((float)std::atof(v.c_str()), -24.f, 12.f);
			else if (k == "cvm")
				cv.set_mode(std::atoi(v.c_str()));
			else if (k == "cvc")
				cv.set_channel(std::atoi(v.c_str()));
			else if (k == "mins") {
				midi_ins = std::clamp(std::atoi(v.c_str()), 0, 2);
				mark_midi_jacks();
			} else if (k == "wifi")
				wifi.set_enabled(std::atoi(v.c_str()) != 0);
			else if (k == "page")
				r.page = std::atoi(v.c_str());
			else if (k == "rel")
				r.rel = v;
			else if (k == "label")
				r.label = v;
			else if (k == "p" || k == "k") {
				size_t i = 0;
				while (i < v.size()) {
					size_t c = v.find(',', i);
					if (c == std::string::npos)
						c = v.size();
					unsigned a = 0, bb = 0;
					float val = 0;
					if (std::sscanf(v.c_str() + i, "%u.%u:%f", &a, &bb, &val) == 3) {
						if (k == "p")
							r.params.push_back({(uint16_t)a, (uint16_t)bb, val});
						else
							r.knobs.push_back({{a, bb}, val});
					}
					i = c + 1;
				}
			}
		}
		{
			std::lock_guard<std::mutex> g{wmx};
			restore = std::move(r);
			if (!library.empty()) {
				int i = find_entry(library, restore.rel, restore.label);
				want_index = i >= 0 ? i : default_index();
			}
			// else: the scan picks it up
		}
		wcv.notify_all();
	}
};

// ---------------------------------------------------------------- FPU: flush denormals while the patch runs

struct DenormalGuard {
#if defined(__arm__)
	uint32_t saved;
	DenormalGuard() {
		asm volatile("vmrs %0, fpscr" : "=r"(saved));
		uint32_t f = saved | (1u << 24);
		asm volatile("vmsr fpscr, %0" : : "r"(f));
	}
	~DenormalGuard() {
		asm volatile("vmsr fpscr, %0" : : "r"(saved));
	}
#elif defined(__x86_64__) || defined(__i386__)
	unsigned saved;
	DenormalGuard() {
		saved = _mm_getcsr();
		_mm_setcsr(saved | 0x8040);
	}
	~DenormalGuard() {
		_mm_setcsr(saved);
	}
#endif
};

} // namespace Meta4ms

// ---------------------------------------------------------------- the wrapper's engine contract

using Meta4ms::Inst;

static void *e_create(const char *data_dir) {
	return new Inst(data_dir);
}
static void e_destroy(void *p) {
	delete static_cast<Inst *>(p);
}
static void e_midi(void *p, const uint8_t *msg, int len) {
	Meta4ms::DenormalGuard g;
	static_cast<Inst *>(p)->midi_in(msg, len);
}
static void e_set(void *p, const char *k, const char *v) {
	static_cast<Inst *>(p)->set_param(k, v);
}
static int e_get(void *p, const char *k, char *buf, int len) {
	return static_cast<Inst *>(p)->get_param(k, buf, len);
}
static void e_render(void *p, int16_t *out, int frames) {
	Meta4ms::DenormalGuard g;
	static_cast<Inst *>(p)->run([](int, float &l, float &r) { l = r = 0; }, out, frames);
}
static void e_process(void *p, const int16_t *in, int16_t *out, int frames) {
	Meta4ms::DenormalGuard g;
	static_cast<Inst *>(p)->run(
		[in](int i, float &l, float &r) {
			l = in[2 * i] * (1.f / 32768.f);
			r = in[2 * i + 1] * (1.f / 32768.f);
		},
		out,
		frames);
}
static void e_song_pos(void *p, double ppq, double bpm, int playing, int frames) {
	static_cast<Inst *>(p)->song_pos(ppq, bpm, playing, frames);
}

extern "C" const mpc_engine_t *mpc_engine(void) {
	static const mpc_engine_t api = {e_create, e_destroy, e_midi, e_set, e_get, e_render,
#ifdef META4MS_FX
									 e_process,
#else
									 nullptr,
#endif
									 e_song_pos};
	return &api;
}

extern "C" int mpc_engine_changed(void *inst, char *key, int len) {
	return static_cast<Inst *>(inst)->next_changed(key, len);
}
