#pragma once
#include "util/math.hh"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

// Bit layout (32 bits total):
// 0b0000 0000 PPPP PPPP CCCC cNNN nnnn nnnn
//
// PPPP PPPP: MIDI Port mask (see PortMaskShift below). All bits clear = listen to
//            every port. This is what patches saved before the MIDI Expander
//            existed decode to, so they keep working on whichever port is used.
// CCCC: MIDI Channel (values 0-15 means channel 1-16)
// c: If 0 then Omni mode (ignore CCCC value). If 1 then use CCCC for MIDI channel
// NNN: Event type: 1 = Note, 2 = CC, 3 = GateOn, 4 = Clock, 5 = Transport
// nnnn nnnn: Data specific to the event type (see below)

// Bits 0-7 have different meanings depending on the Event Type.
// Event types:
// 0x1nn    Note-based events
//     ^--- Polychannel: (bits 0-3): 0-7 (which poly note the event refers to)
//    ^---- Event (bits 4-7): 0=Note, 1=Gate, 2=Vel, 3=Aft, 4=Retrig
//   ^----- Event type (bits 8-11) == 1: MIDI Note-based event
//
// 0x2nn    CC
//    ^^--- 00-7F: CC number, or 0x80 == PitchWheel
//   ^----- Event type (bits 8-11) == 2: MIDI CC
//
// 0x3nn    Gate on Note events
//    ^^--- 00-7F: note number
//   ^----- Event type (bits 8-11) == 3: MIDI GateNote (gate held high while note is pressed)
//
// 0x4nn    Clock
//    ^^--- division amount (relative to 24ppqn)
//   ^----- Event type (bits 8-11) == 4: MIDI Clock
//
//          Transport
// 0x501: Start
// 0x501: Stop
// 0x502: Continue
//
// 0x6xx: unused
// 0x7xx: unused
//
// Note: Bit 11 (0x800) must be set in order for MIDI channel to be considered valid
// Note: Channel does not have any meaning for MIDI Clock or Transport events
//

enum MidiMappings : uint32_t {
	MidiMonoNoteJack = 0x100,
	MidiNote2Jack,
	MidiNote3Jack,
	MidiNote4Jack,
	MidiNote5Jack,
	MidiNote6Jack,
	MidiNote7Jack,
	MidiNote8Jack,

	MidiMonoGateJack = 0x110,
	MidiGate2Jack,
	MidiGate3Jack,
	MidiGate4Jack,
	MidiGate5Jack,
	MidiGate6Jack,
	MidiGate7Jack,
	MidiGate8Jack,

	MidiMonoVelJack = 0x120,
	MidiVel2Jack,
	MidiVel3Jack,
	MidiVel4Jack,
	MidiVel5Jack,
	MidiVel6Jack,
	MidiVel7Jack,
	MidiVel8Jack,

	MidiMonoAftertouchJack = 0x130,
	MidiAftertouch2Jack,
	MidiAftertouch3Jack,
	MidiAftertouch4Jack,
	MidiAftertouch5Jack,
	MidiAftertouch6Jack,
	MidiAftertouch7Jack,
	MidiAftertouch8Jack,

	MidiMonoRetrigJack = 0x140,
	MidiRetrig2Jack,
	MidiRetrig3Jack,
	MidiRetrig4Jack,
	MidiRetrig5Jack,
	MidiRetrig6Jack,
	MidiRetrig7Jack,
	MidiRetrig8Jack,

	// poly chans 1-4:
	MidiNotePolyJack = 0x150,
	MidiGatePolyJack = 0x151,
	MidiVelPolyJack = 0x152,
	MidiAftPolyJack = 0x153,
	MidiRetrigPolyJack = 0x154,

	// poly chans 5-8:
	MidiNotePoly5_8Jack = 0x155,
	MidiGatePoly5_8Jack = 0x156,
	MidiVelPoly5_8Jack = 0x157,
	MidiAftPoly5_8Jack = 0x158,
	MidiRetrigPoly5_8Jack = 0x159,

	MidiCC0 = 0x200,
	MidiCC1, // MidiModWheelJack,
	//...
	MidiCC127 = 0x27F,
	MidiPitchWheelJack = 0x280,

	MidiGateNote0 = 0x300, //Note 0 (C-2) -> Gate
	//...
	MidiGateNote127 = 0x37F, //Note 127 (G8) -> Gate

	MidiClockJack = 0x400,			 // 24 PPQN clock (not divided)
	MidiClockDiv1Jack = 0x400 + 1,	 // 24 PPQN (alias for MidiClockJack)
	MidiClockDiv2Jack = 0x400 + 2,	 // 12 PPQN
	MidiClockDiv3Jack = 0x400 + 3,	 // 32nd note = 3 pulses
	MidiClockDiv6Jack = 0x400 + 6,	 // 16th note = 6 pulses
	MidiClockDiv12Jack = 0x400 + 12, // 8th note = 12 pulses
	MidiClockDiv24Jack = 0x400 + 24, // Quarter note = 24 pulses
	MidiClockDiv48Jack = 0x400 + 48, // Half notes = 2 quarter notes
	MidiClockDiv96Jack = 0x400 + 96, // Whole note = 4 quarter notes

	MidiStartJack = 0x500,
	MidiStopJack,
	MidiContinueJack,

	LastMidiJack
};

namespace TimingEvents
{
enum : uint8_t { Clock = 0, Start = 1, Stop = 2, Cont = 3, NumTimingEvents };
} // namespace TimingEvents

enum { MidiModWheelJack = MidiCC1 };

static constexpr unsigned MaxMidiPolyphony = 8;
static constexpr unsigned MaxMidiPolyChannels = 4; // Matches CoreProcessor::MaxPolyChannels

static constexpr size_t NumMidiNotes = 128;
static constexpr size_t NumMidiCCs = 128;
static constexpr size_t NumMidiCCsPW = NumMidiCCs + 1; //plus pitch wheel (aka bend)
static constexpr size_t NumMidiClockJacks = MidiClockDiv96Jack - MidiClockJack + 1;

namespace MetaModule::Midi
{

static constexpr unsigned PitchBendCC = 128;

// Converts MIDI 7-bit to volts
template<unsigned MaxVolts>
constexpr float u7_to_volts(uint8_t val) {
	return (float)val / (127.f / (float)MaxVolts);
}

// Converts a CC value to volts. The M4 core sends all CC values as 14-bit: in 7-bit
// mode it left-shifts the 7-bit value by 7 (so 127 => 127<<7 = 16256), and in 14-bit
// mode it combines the MSB (CC 0-31) and LSB (CC 32-63) into a 0..16383 value. We scale
// so 7-bit full-scale (16256) maps to MaxVolts exactly, preserving legacy behavior;
// 14-bit full-scale (16383) therefore reaches very slightly above MaxVolts (~0.8%).
template<unsigned MaxVolts>
constexpr float u14cc_to_volts(int16_t val) {
	return (float)val * (float)MaxVolts / (127.f * 128.f);
}
static_assert(u14cc_to_volts<10>(127 << 7) == 10.f);
static_assert(u14cc_to_volts<10>(0) == 0.f);

template<unsigned NumSemitones>
constexpr float s14_to_semitones(int16_t val) {
	return (float)val / (8192.f / ((float)NumSemitones / 12.f));
}
static_assert(s14_to_semitones<2>(-8192) == -0.16666667f);
static_assert(s14_to_semitones<2>(8192) == 0.16666667f);

// Note 60 = C5 means use the freq. set by the panel controls
constexpr float note_to_volts(uint8_t note) {
	return (note - 60) / 12.f;
}
static_assert(note_to_volts(60) == 0);
static_assert(note_to_volts(72) == 1);

constexpr uint32_t strip_midi_channel(uint32_t panel_jack_id) {
	return panel_jack_id & 0x07FF; //clear channel bits and omni bit
}

// Returns 0 for Omni, or 1-16 for MIDI channel 1-16
constexpr uint32_t midi_channel(uint32_t panel_jack_id) {
	if ((panel_jack_id & 0x0800) && strip_midi_channel(panel_jack_id) < 0x400)
		return ((panel_jack_id >> 12) & 0xF) + 1; //1-16
	else
		return 0;
}

// Port mask: bit N set means "ignore messages that arrived on port N".
// A mask of 0 listens to every port. Port numbers match MetaModule::Midi::Event::Port
// (0=USB, 1=TRS, 2=DIN5)
static constexpr uint32_t PortMaskShift = 16;
static constexpr uint32_t PortMaskBits = 0xFFu << PortMaskShift;
static constexpr uint8_t NumPorts = 3;
static constexpr uint8_t AllPorts = 0;

constexpr uint8_t port_mask(uint32_t panel_jack_id) {
	return uint8_t((panel_jack_id & PortMaskBits) >> PortMaskShift);
}

constexpr MidiMappings set_port_mask(uint32_t panel_jack_id, uint8_t mask) {
	return MidiMappings((panel_jack_id & ~PortMaskBits) | (uint32_t(mask) << PortMaskShift));
}

constexpr uint32_t strip_port(uint32_t panel_id) {
	return set_port_mask(panel_id, 0);
}

// The mask that listens to exactly one port
constexpr uint8_t only_port(uint8_t port) {
	return uint8_t(((1u << NumPorts) - 1) & ~(1u << port));
}

// Should a mapping with this mask act on a message that arrived on `port`?
constexpr bool port_allows(uint8_t mask, uint8_t port) {
	return (mask & (1u << port)) == 0;
}

// The single port a mask selects, or nullopt for "all ports". Also nullopt for a
// mask naming more than one port, which the GUI can't produce but a hand-edited
// patch could.
constexpr std::optional<uint8_t> selected_port(uint8_t mask) {
	for (uint8_t port = 0; port < NumPorts; port++) {
		if (mask == only_port(port))
			return port;
	}
	return std::nullopt;
}

// midi_chan: 1-16 for a MIDI Channel. 0 for Omni
constexpr MidiMappings set_midi_channel(uint32_t panel_jack_id, uint32_t midi_chan) {
	// strip_midi_channel() drops the port bits along with the channel, so carry them over
	const auto ports = panel_jack_id & PortMaskBits;

	if (midi_chan >= 1 && midi_chan <= 16)
		return MidiMappings(strip_midi_channel(panel_jack_id) | ports | 0x0800 | ((midi_chan - 1) << 12));
	else
		return MidiMappings(strip_midi_channel(panel_jack_id) | ports);
}

static_assert(port_mask(MidiCC0) == AllPorts, "Legacy mappings must listen to every port");
static_assert(port_allows(AllPorts, 0) && port_allows(AllPorts, 1) && port_allows(AllPorts, 2));
static_assert(port_allows(only_port(1), 1) && !port_allows(only_port(1), 0) && !port_allows(only_port(1), 2));
static_assert(port_mask(set_port_mask(MidiCC0, only_port(2))) == only_port(2));
static_assert(strip_midi_channel(set_port_mask(MidiCC0, only_port(2))) == MidiCC0);
static_assert(!selected_port(AllPorts).has_value());
static_assert(selected_port(only_port(2)).value() == 2);
// Channel and port survive each other's setters, in either order
static_assert(midi_channel(set_midi_channel(set_port_mask(MidiCC0, only_port(1)), 7)) == 7);
static_assert(port_mask(set_midi_channel(set_port_mask(MidiCC0, only_port(1)), 7)) == only_port(1));
static_assert(port_mask(set_port_mask(set_midi_channel(MidiCC0, 7), only_port(1))) == only_port(1));
static_assert(midi_channel(set_port_mask(set_midi_channel(MidiCC0, 7), only_port(1))) == 7);
// Clearing the channel back to Omni must not disturb the port
static_assert(port_mask(set_midi_channel(set_port_mask(MidiCC0, only_port(1)), 0)) == only_port(1));

constexpr bool is_midi_poly_cable(uint32_t id) {
	id = strip_midi_channel(id);
	return id >= MidiNotePolyJack && id <= MidiRetrigPoly5_8Jack;
}

// True only for the poly channels 5-8 cable (as opposed to the 1-4 cable)
constexpr bool is_midi_poly5_8_cable(uint32_t id) {
	id = strip_midi_channel(id);
	return id >= MidiNotePoly5_8Jack && id <= MidiRetrigPoly5_8Jack;
}

// Poly channel offset carried by a poly cable: 0 for the 1-4 cable, 4 for the 5-8 cable
static constexpr uint8_t MidiPolyCableChanBase = MaxMidiPolyChannels;

// Returns 1-8 for the poly chan of a MidiMapping
constexpr std::optional<uint8_t> polychan(unsigned mapping) {
	mapping = strip_midi_channel(mapping);
	if (mapping >= MidiMonoNoteJack && mapping < MidiNotePolyJack) {
		return std::min<uint8_t>(mapping & 0x0F, 7) + 1;
	}
	return std::nullopt;
}

static_assert(polychan(MidiMonoNoteJack).value() == 1);
static_assert(polychan(MidiNote2Jack).value() == 2);
static_assert(polychan(MidiNote8Jack).value() == 8);
static_assert(polychan(MidiRetrig2Jack).value() == 2);
static_assert(polychan(MidiRetrig8Jack).value() == 8);
static_assert(polychan(MidiMonoNoteJack + /*no omni:*/ (1 << 11) + /*channel 7*/ (6 << 12)).value() == 1);
static_assert(polychan(MidiNote2Jack + /*no omni:*/ (1 << 11) + /*channel 16*/ (0xF << 12)).value() == 2);
static_assert(!polychan(MidiNotePolyJack).has_value());
static_assert(!polychan(MidiRetrigPolyJack).has_value());
static_assert(!polychan(MidiNotePoly5_8Jack).has_value());
static_assert(!polychan(MidiRetrigPoly5_8Jack).has_value());
static_assert(is_midi_poly_cable(MidiNotePolyJack));
static_assert(is_midi_poly_cable(MidiRetrigPolyJack));
static_assert(is_midi_poly_cable(MidiNotePoly5_8Jack));
static_assert(is_midi_poly_cable(MidiRetrigPoly5_8Jack));
static_assert(!is_midi_poly_cable(MidiMonoNoteJack));
static_assert(!is_midi_poly_cable(MidiCC0));
static_assert(is_midi_poly5_8_cable(MidiNotePoly5_8Jack));
static_assert(is_midi_poly5_8_cable(MidiRetrigPoly5_8Jack));
static_assert(!is_midi_poly5_8_cable(MidiNotePolyJack));
static_assert(!is_midi_poly5_8_cable(MidiRetrigPolyJack));

constexpr std::optional<uint32_t> midi_note_pitch(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiMonoNoteJack, MidiNote8Jack);
}

constexpr std::optional<uint32_t> midi_note_gate(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiMonoGateJack, MidiGate8Jack);
}

constexpr std::optional<uint32_t> midi_note_vel(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiMonoVelJack, MidiVel8Jack);
}

constexpr std::optional<uint32_t> midi_note_aft(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiMonoAftertouchJack, MidiAftertouch8Jack);
}

constexpr std::optional<uint32_t> midi_note_retrig(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiMonoRetrigJack, MidiRetrig8Jack);
}

constexpr bool midi_note_pitch_poly(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return panel_jack_id == MidiNotePolyJack;
}

constexpr bool midi_note_gate_poly(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return panel_jack_id == MidiGatePolyJack;
}

constexpr bool midi_note_vel_poly(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return panel_jack_id == MidiVelPolyJack;
}

constexpr bool midi_note_aft_poly(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return panel_jack_id == MidiAftPolyJack;
}

constexpr bool midi_note_retrig_poly(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return panel_jack_id == MidiRetrigPolyJack;
}

constexpr bool midi_note_pitch_poly5_8(uint32_t panel_jack_id) {
	return strip_midi_channel(panel_jack_id) == MidiNotePoly5_8Jack;
}

constexpr bool midi_note_gate_poly5_8(uint32_t panel_jack_id) {
	return strip_midi_channel(panel_jack_id) == MidiGatePoly5_8Jack;
}

constexpr bool midi_note_vel_poly5_8(uint32_t panel_jack_id) {
	return strip_midi_channel(panel_jack_id) == MidiVelPoly5_8Jack;
}

constexpr bool midi_note_aft_poly5_8(uint32_t panel_jack_id) {
	return strip_midi_channel(panel_jack_id) == MidiAftPoly5_8Jack;
}

constexpr bool midi_note_retrig_poly5_8(uint32_t panel_jack_id) {
	return strip_midi_channel(panel_jack_id) == MidiRetrigPoly5_8Jack;
}

// Returns the event index (0=Note, 1=Gate, 2=Vel, 3=Aft, 4=Retrig) for either poly
// cable (1-4 or 5-8), or nullopt if the id is not a poly cable.
constexpr std::optional<uint8_t> midi_poly_cable_event(uint32_t panel_jack_id) {
	auto id = strip_midi_channel(panel_jack_id);
	if (id >= MidiNotePolyJack && id <= MidiRetrigPolyJack)
		return uint8_t(id - MidiNotePolyJack);
	if (id >= MidiNotePoly5_8Jack && id <= MidiRetrigPoly5_8Jack)
		return uint8_t(id - MidiNotePoly5_8Jack);
	return std::nullopt;
}

constexpr std::optional<uint32_t> midi_gate(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiGateNote0, MidiGateNote127);
}

constexpr std::optional<uint32_t> midi_cc(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiCC0, MidiPitchWheelJack);
}

constexpr std::optional<uint32_t> midi_clk(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return panel_jack_id == MidiClockJack ? std::optional<uint32_t>{0} : std::nullopt;
}

constexpr std::optional<uint32_t> midi_divclk(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiClockDiv1Jack, MidiClockDiv96Jack);
}

constexpr std::optional<uint32_t> midi_transport(uint32_t panel_jack_id) {
	panel_jack_id = strip_midi_channel(panel_jack_id);
	return MathTools::between<uint32_t>(panel_jack_id, MidiStartJack, MidiContinueJack);
}

constexpr bool is_midi_panel_id(uint32_t id) {
	auto sid = Midi::strip_midi_channel(id);
	return (sid >= MidiMonoNoteJack && sid < LastMidiJack);
}

}; // namespace MetaModule::Midi
