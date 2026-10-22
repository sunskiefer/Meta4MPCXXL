#pragma once
#include "mapping_ids.hh"
#include "midi_def.hh"
#include "util/math.hh"
#include "util/static_string.hh"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

constexpr unsigned MaxKnobSets = 8;

struct Jack {
	uint16_t module_id;
	uint16_t jack_id;
	bool operator==(const Jack &other) const {
		return this->module_id == other.module_id && this->jack_id == other.jack_id;
	}
};

struct StaticParam {
	uint16_t module_id;
	uint16_t param_id;
	float value;
};

using AliasNameString = StaticString<31>;

struct MappedKnob {
	uint16_t panel_knob_id;
	uint16_t module_id;
	uint16_t param_id;

	enum CurveType : uint8_t { Normal, Toggle, Cycle };
	uint8_t curve_type;

	uint8_t midi_chan : 5;		//0: ignore, 1-16: only MIDI channel 1-16
	uint8_t midi_port_mask : 3; //0: all ports. Bit N set = ignore port N (Midi::port_allows)

	float min;
	float max;
	AliasNameString alias_name;

	// Returns the value of the mapped knob, given the panel knob value
	// Return value goes from min to max as panel_val goes from 0 to 1
	// If max < min then mapping will be reversed direction
	float get_mapped_val(float panel_val) const {
		return (max - min) * panel_val + min;
	}

	float unmap_val(float mapped_val) const {
		if (min == max)
			return 0;
		return (mapped_val - min) / (max - min);
	}

	// TODO: move to free function in boards/euro26hp
	bool is_panel_knob() const {
		return panel_knob_id < MaxPanelKnobs;
	}

	bool is_midi_cc() const {
		return (panel_knob_id >= MidiCC0 && panel_knob_id <= MidiCC127);
	}

	bool is_midi_notegate() const {
		return (panel_knob_id >= MidiGateNote0 && panel_knob_id <= MidiGateNote127);
	}

	bool is_midi() const {
		return is_midi_notegate() || is_midi_cc();
	}

	// TODO: move to free function in boards/euro26hp
	bool is_button() const {
		return panel_knob_id >= FirstButton && panel_knob_id <= LastButton;
	}

	// TODO: move to free function in boards/euro26hp
	std::optional<uint32_t> ext_button() const {
		using namespace MetaModule;
		return MathTools::between<uint32_t>(panel_knob_id, FirstButton, LastButton);
	}

	uint16_t cc_num() const {
		return panel_knob_id - MidiCC0;
	}

	uint16_t notegate_num() const {
		return panel_knob_id - MidiGateNote0;
	}

	bool operator==(const MappedKnob &other) const {
		return maps_to_same_as(other);
	}

	bool maps_to_same_as(const MappedKnob &other) const {
		return (module_id == other.module_id) && (param_id == other.param_id);
	}
};

struct MappedKnobSet {
	std::vector<MappedKnob> set;
	AliasNameString name{};
};

struct InternalCable {
	Jack out{};
	std::vector<Jack> ins;
	std::optional<uint16_t> color{};
};

struct MappedInputJack {
	uint32_t panel_jack_id{};
	std::vector<Jack> ins;
	AliasNameString alias_name{};
};

struct MappedOutputJack {
	uint32_t panel_jack_id{};
	Jack out{};
	AliasNameString alias_name{};
};

struct ModuleInitState {
	uint32_t module_id{};
	std::string state_data;
};

struct ModuleAlias {
	uint16_t module_id{};
	AliasNameString alias_name{};
};

// Position of a module in the patch layout, in grid units (x: HP, y: rows).
// The top-left of the layout is 0,0
struct ModulePosition {
	uint16_t module_id{};
	int16_t x{};
	int16_t y{};
};

struct MappedLight {
	uint32_t panel_light_id{};
	uint16_t module_id{};
	uint16_t light_id{};
};

enum class ExpanderSide : uint8_t { Left = 0, Right = 1 };
inline std::string_view expander_side_name(ExpanderSide side) {
	return side == ExpanderSide::Left ? "Left" : "Right";
}

// right_module_id is attached as the right-side expander of left_module_id
struct ExpanderConnection {
	uint16_t left_module_id{};
	uint16_t right_module_id{};

	// The pair of modules an expander connection would create, given a module, which side
	// of it, and the module to attach there
	static ExpanderConnection make(uint16_t module_id, ExpanderSide side, uint16_t other_id) {
		return side == ExpanderSide::Left ?
				   ExpanderConnection{.left_module_id = other_id, .right_module_id = module_id} :
				   ExpanderConnection{.left_module_id = module_id, .right_module_id = other_id};
	}
};

enum class PolyMode { Rotate, Reuse, Reset, Mpe };

static_assert(sizeof(Jack) == 4, "Jack should be 4B");
static_assert(sizeof(StaticParam) == 8, "StaticParam should be 8B");
static_assert(sizeof(AliasNameString) == 32, "AliasNameString should be 32B");
static_assert(sizeof(ModuleAlias) == 34, "ModuleAlias should be 34B");
static_assert(sizeof(ExpanderConnection) == 4, "ExpanderConnection should be 4B");
static_assert(sizeof(ModulePosition) == 6, "ModulePosition should be 6B");
static_assert(sizeof(MappedKnob) == 48, "MappedKnob should be 48B");
static_assert(MetaModule::Midi::NumPorts <= 3, "MappedKnob::midi_port_mask has only 3 bits");
static_assert(sizeof(MappedOutputJack) == 40, "MappedOutputJack should be 40B");
