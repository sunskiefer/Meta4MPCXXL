// Meta4msXXL: the Q-Link pages of a loaded patch.
//
// A MetaModule patch keeps every parameter of every module (static_knobs) and, on top, its knob sets: the hub's
// physical knobs mapped to one or more module parameters each. The plugin shows both as pages of 8, the eight
// Q-Links of the Force's first knob bank:
//   1. each knob set, one item per hub knob (all of that knob's maps move together, as on the module),
//   2. each module in patch order, every parameter it has, in the module's own element order.
#pragma once
#include "CoreModules/elements/elements.hh"
#include "patch/patch_data.hh"
#include <string>
#include <vector>

namespace MetaModule
{
class PatchPlayer;
}

namespace Meta4ms
{

constexpr unsigned SlotsPerPage = 8;

struct Item {
	enum class Kind { Param, KnobSetKnob } kind = Kind::Param;
	std::string name;							  // knob label, upper case
	uint16_t module_id = 0, param_id = 0;		  // Param: the parameter; KnobSetKnob: the first map's
	const MetaModule::Element *element = nullptr; // for value text and switch positions
	unsigned positions = 0;						  // > 1: a switch / stepped control with that many positions
	bool button = false;						  // momentary or latching button: 0 or 1
	unsigned knob_set = 0;
	uint16_t panel_knob = 0;
	std::vector<::MappedKnob> maps; // KnobSetKnob: every param this knob moves
	float knob_pos = 0;						  // KnobSetKnob: the knob's own position 0..1
};

struct Page {
	std::string title; // e.g. "DJEMBE 1/2"
	std::vector<Item> items;
};

struct Pages {
	std::vector<Page> pages;
	unsigned knob_set_pages = 0; // the first pages that come from knob sets

	void build(MetaModule::PatchData const &pd, MetaModule::PatchPlayer &player);

	// value of an item, 0..1
	static float get(Item const &it, MetaModule::PatchPlayer &player);
	// set an item from a slot value 0..1 (switches snap, buttons are 0/1, knob-set knobs move all their maps)
	static void set(Item &it, float v, MetaModule::PatchPlayer &player);
	// value text the way MetaModule shows it ("440Hz", "35%", "Saw")
	static std::string value_text(Item const &it, MetaModule::PatchPlayer &player);
};

std::string module_title(MetaModule::PatchData const &pd, unsigned module_id);

} // namespace Meta4ms
