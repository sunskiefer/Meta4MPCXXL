// Meta4msXXL: the patch's extra outputs (hub Out 3-8) as MIDI on an ALSA sequencer port of the plugin's own.
//
// MPC OS ignores a plugin's VST MIDI output, but lists an ALSA sequencer port a plugin opens as a MIDI input device
// (mpc-vst-plugins docs/NOTES.md, poc/midiport.c). A Force CV track set to listen to that port turns the notes into
// pitch + gate on the CV/Gate jacks. libasound is dlopen()ed, so nothing extra is needed to build or load the plugin,
// and the port only exists once CV output is switched on.
//
// Modes (parameter "cv_mode"):
//   Off
//   Notes + CC  Out 3 = pitch (1 V/oct, 0 V = C4 = MIDI note 60), Out 4 = gate (on above 1 V): notes;
//               Out 5-8 = CC 16-19 (0..10 V -> 0..127)
//   CC only     Out 3-8 = CC 16-21 (0..10 V -> 0..127)
// The audio thread only samples the outputs and queues events; a thread of the port's own sends them.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace Meta4ms
{

class CvPort {
public:
	enum Mode { Off = 0, NotesCC = 1, CCOnly = 2 };

	explicit CvPort(std::string client_name);
	~CvPort();

	void set_mode(int m);
	void set_channel(int ch1to16);
	int mode() const {
		return mode_;
	}
	int channel() const {
		return chan_ + 1;
	}
	bool port_open() const {
		return port_ >= 0;
	}

	// audio thread: hub outputs 3..8 in volts, called every few samples
	void sample(const float *volts6, unsigned frames_since_last);
	// audio thread: release everything (patch change, mode change)
	void all_off();

private:
	struct Msg {
		uint8_t b[3];
	};
	static constexpr unsigned QSize = 512;
	std::array<Msg, QSize> q_{};
	std::atomic<unsigned> head_{0}, tail_{0};
	void push(uint8_t a, uint8_t b, uint8_t c);

	std::string name_;
	std::atomic<int> mode_{Off};
	int chan_ = 0;
	int note_ = -1;
	bool gate_ = false;
	std::array<int, 6> cc_last_{{-1, -1, -1, -1, -1, -1}};
	std::array<unsigned, 6> cc_wait_{};

	// sender thread
	std::thread thr_;
	std::atomic<bool> quit_{false};
	void run();
	void *lib_ = nullptr;
	void *seq_ = nullptr;
	int port_ = -1;
	bool open_port();
	void send(Msg const &m);
};

} // namespace Meta4ms
