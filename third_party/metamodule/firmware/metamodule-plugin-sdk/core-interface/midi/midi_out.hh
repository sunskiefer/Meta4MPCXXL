#pragma once
#include "midi/midi_message.hh"
#include <memory>

namespace MetaModule
{

struct MidiOutput {
	struct Internal;
	std::unique_ptr<Internal> internal;

	MidiOutput();
	~MidiOutput();

	void push_message(MidiMessage msg);
	bool is_queue_full() const;
};

} // namespace MetaModule
