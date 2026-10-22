#pragma once
#include "midi/midi_message.hh"
#include <memory>
#include <optional>

namespace MetaModule
{

struct MidiInput {
	struct Internal;
	std::unique_ptr<Internal> internal;

	MidiInput();

	~MidiInput();

	// Return a the next MIDI message, or std::nullopt if there are no new messages
	std::optional<MidiMessage> pop_message();

	// Same as pop_message() but with a different interface:
	// if there is no message it does nothing and returns false.
	// If there is a message, it returns true and copies the message to
	// the provided message ptr.
	bool pop_message(MidiMessage *message);
};

} // namespace MetaModule
