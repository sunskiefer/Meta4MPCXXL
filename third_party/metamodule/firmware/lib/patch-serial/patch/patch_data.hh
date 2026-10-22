#pragma once
#include "module_type_slug.hh"
#include "patch.hh"

#include <algorithm>
#include <optional>
#include <vector>

namespace MetaModule
{

struct PatchData {
	static constexpr size_t DescSize = 255;
	PatchName patch_name{""};
	StaticString<DescSize> description;
	std::vector<BrandModuleSlug> module_slugs;
	std::vector<InternalCable> int_cables;
	std::vector<MappedInputJack> mapped_ins;
	std::vector<MappedOutputJack> mapped_outs;
	std::vector<StaticParam> static_knobs;
	std::vector<MappedKnobSet> knob_sets;
	std::vector<MappedLight> mapped_lights;
	std::vector<ModuleInitState> module_states;
	MappedKnobSet midi_maps;
	std::vector<uint16_t> bypassed_modules;
	std::vector<ModuleAlias> module_aliases;
	std::vector<ExpanderConnection> expanders;
	std::vector<ModulePosition> module_positions;

	std::vector<uint16_t> module_cores;
	std::vector<uint32_t> module_loads; //units in ppm of one core

	uint32_t midi_poly_num = 1;
	// User-set max poly channels: 0 = Auto (compute from cables), 1-8 = hard-set midi_poly_num
	uint16_t midi_poly_num_setting = 0;
	PolyMode midi_poly_mode = PolyMode::Rotate;
	float midi_pitchwheel_range = 1.f;

	uint32_t suggested_samplerate;
	uint32_t suggested_blocksize;

	static constexpr uint32_t MIDIKnobSet = 0xFFFFFFFF;

	void blank_patch(std::string_view patch_name) {
		*this = PatchData{};
		this->patch_name.copy(patch_name);
		module_slugs.push_back("HubMedium");
		knob_sets.push_back({{}, "Knob Set 1"});
		midi_maps.name = "MIDI";
	}

	// True if a previously calculated load balance is present and still matches the modules
	bool has_load_balance(unsigned num_cores) const {
		if (module_cores.size() != module_slugs.size())
			return false;
		if (module_loads.size() != module_slugs.size())
			return false;
		return std::ranges::all_of(module_cores, [=](uint16_t core) { return core < num_cores; });
	}

	void clear_load_balance() {
		module_cores.clear();
		module_loads.clear();
	}

	const MappedKnob *find_mapped_knob(uint32_t set_id, uint32_t module_id, uint32_t param_id) const {
		if (set_id == MIDIKnobSet)
			return find_midi_map(module_id, param_id);

		if (set_id < knob_sets.size()) {
			for (auto &m : knob_sets[set_id].set) {
				if (m.module_id == module_id && m.param_id == param_id)
					return &m;
			}
		}
		return nullptr;
	}

	const MappedKnob *find_mapped_knob(uint32_t set_id, uint16_t panel_knob_id) const {
		if (set_id == MIDIKnobSet)
			return find_midi_map(panel_knob_id);

		if (set_id < knob_sets.size()) {
			for (auto &m : knob_sets[set_id].set) {
				if (m.panel_knob_id == panel_knob_id)
					return &m;
			}
		}
		return nullptr;
	}

	const std::optional<uint32_t> find_mapped_knob_idx(uint32_t set_id, uint32_t module_id, uint32_t param_id) const {
		if (set_id != MIDIKnobSet && set_id >= knob_sets.size())
			return std::nullopt;

		auto &knobset = (set_id == MIDIKnobSet) ? midi_maps.set : knob_sets[set_id].set;

		auto mk = std::find_if(knobset.cbegin(), knobset.cend(), [=](auto m) {
			return (m.module_id == module_id && m.param_id == param_id);
		});

		if (mk != knobset.end())
			return std::distance(knobset.begin(), mk);
		else
			return std::nullopt;
	}

	const MappedKnob *find_midi_map(uint32_t module_id, uint32_t param_id) const {
		for (auto &m : midi_maps.set) {
			if (m.module_id == module_id && m.param_id == param_id)
				return &m;
		}
		return nullptr;
	}

	const MappedKnob *find_midi_map(uint16_t panel_knob_id) const {
		for (auto &m : midi_maps.set) {
			if (m.panel_knob_id == panel_knob_id)
				return &m;
		}
		return nullptr;
	}

	void update_midi_poly_num() {
		// User hard-set the count: ignore cables
		if (midi_poly_num_setting > 0) {
			midi_poly_num = midi_poly_num_setting;
			return;
		}

		uint32_t max_poly_used = 0;

		for (auto const &map : mapped_ins) {
			if (Midi::is_midi_poly5_8_cable(map.panel_jack_id)) {
				// 5-8 cable carries poly channels 5-8, so it needs the full polyphony allocated
				max_poly_used = std::max<uint32_t>(max_poly_used, MaxMidiPolyphony);
			} else if (Midi::is_midi_poly_cable(map.panel_jack_id)) {
				max_poly_used = std::max<uint32_t>(max_poly_used, MaxMidiPolyChannels);
			} else if (auto num = Midi::polychan(map.panel_jack_id)) {
				max_poly_used = std::max<uint32_t>(max_poly_used, *num);
			}
		}
		midi_poly_num = max_poly_used;
	}

	// Updates an existing mapped knob, or adds it if it doesn't exist yet
	bool add_update_mapped_knob(uint32_t set_id, MappedKnob const &map) {
		if (set_id == MIDIKnobSet)
			return add_update_midi_map(map);

		if (set_id >= MaxKnobSets)
			return false;

		if (map.module_id >= module_slugs.size())
			return false;

		if (set_id > knob_sets.size())
			return false;

		if (set_id == knob_sets.size()) {
			// Append a new knob set and add a mapping to it
			knob_sets.push_back({});
			knob_sets[set_id].set.push_back(map);

		} else if (auto *m = _get_mapped_knob(set_id, map.module_id, map.param_id)) {
			// Update a mapping in an existing knob set
			*m = map;

		} else {
			// Add a new mapping in an existing knob set
			knob_sets[set_id].set.push_back(map);
		}

		return true;
	}

	bool add_update_midi_map(MappedKnob const &map) {
		if (!map.is_midi())
			return false;

		if (map.module_id >= module_slugs.size())
			return false;

		if (auto *m = _get_midi_map(map.module_id, map.param_id)) {
			*m = map;
		} else {
			midi_maps.set.push_back(map);
		}

		return true;
	}

	bool remove_mapping(uint32_t set_id, MappedKnob const &map) {
		if (set_id != MIDIKnobSet && set_id >= knob_sets.size())
			return false;

		if (map.module_id >= module_slugs.size())
			return false;

		MappedKnobSet &set = (set_id == MIDIKnobSet) ? midi_maps : knob_sets[set_id];

		auto num_erased = std::erase_if(
			set.set, [&map](auto const &m) { return (m.module_id == map.module_id && m.param_id == map.param_id); });

		return num_erased > 0;
	}

	const StaticParam *find_static_knob(uint32_t module_id, uint32_t param_id) const {
		for (auto &m : static_knobs) {
			if (m.module_id == module_id && m.param_id == param_id) {
				return &m;
			}
		}
		return nullptr;
	}

	std::optional<float> get_static_knob_value(uint16_t module_id, uint16_t param_id) const {
		for (auto const &m : static_knobs) {
			if (m.module_id == module_id && m.param_id == param_id) {
				return m.value;
			}
		}
		return std::nullopt;
	}

	void set_or_add_static_knob_value(uint32_t module_id, uint32_t param_id, float val) {
		for (auto &m : static_knobs) {
			if (m.module_id == module_id && m.param_id == param_id) {
				m.value = val;
				return;
			}
		}
		// Not found, add it
		static_knobs.push_back({(uint16_t)module_id, (uint16_t)param_id, val});
	}

	void add_internal_cable(Jack in, Jack out) {
		if (auto existing_cable_out = _find_internal_cable_with_outjack(out))
			existing_cable_out->ins.push_back(in);
		else
			int_cables.push_back({out, {in}});
	}

	void disconnect_injack(Jack jack) {
		// Remove from inputs on all internal cables
		for (auto &cable : int_cables) {
			std::erase(cable.ins, jack);
		}
		// Remove any cables that now have no inputs
		std::erase_if(int_cables, [](auto const &cable) { return (cable.ins.size() == 0); });

		remove_injack_mappings(jack);
	}

	void disconnect_outjack(Jack jack) {
		// Remove any cables with this output
		std::erase_if(int_cables, [jack](auto const &cable) { return (cable.out == jack); });

		remove_outjack_mappings(jack);
	}

	void remove_injack_mappings(Jack jack) {
		// Remove from inputs on all panel mappings
		for (auto &map : mapped_ins) {
			std::erase(map.ins, jack);
		}
		// Remove any panel mappings that now have no inputs
		std::erase_if(mapped_ins, [](auto const &map) { return (map.ins.size() == 0); });

		update_midi_poly_num();
	}

	void remove_outjack_mappings(Jack jack) {
		// Remove any panel mappings with this output
		std::erase_if(mapped_outs, [jack](auto const &map) { return (map.out == jack); });
	}

	// Removes one input from the internal cable driven by `out`, leaving the cable's other inputs connected
	bool remove_internal_cable(Jack out, Jack in) {
		auto *cable = _find_internal_cable_with_outjack(out);
		if (!cable)
			return false;

		auto num_erased = std::erase(cable->ins, in);

		// Remove any cables that now have no inputs
		std::erase_if(int_cables, [](auto const &cable) { return (cable.ins.size() == 0); });

		return num_erased > 0;
	}

	// Removes one panel (or MIDI) input mapping to `jack`, leaving its other mappings in place
	bool remove_mapped_injack(uint32_t panel_jack_id, Jack jack) {
		size_t num_erased = 0;
		for (auto &map : mapped_ins) {
			if (map.panel_jack_id == panel_jack_id)
				num_erased += std::erase(map.ins, jack);
		}
		// Remove any panel mappings that now have no inputs
		std::erase_if(mapped_ins, [](auto const &map) { return (map.ins.size() == 0); });

		update_midi_poly_num();

		return num_erased > 0;
	}

	// Removes one panel output mapping from `jack`, leaving its other mappings in place
	bool remove_mapped_outjack(uint32_t panel_jack_id, Jack jack) {
		auto num_erased = std::erase_if(
			mapped_outs, [=](auto const &map) { return (map.panel_jack_id == panel_jack_id && map.out == jack); });
		return num_erased > 0;
	}

	const MappedInputJack *find_mapped_midi_injack(Jack jack) const {
		for (auto &m : mapped_ins) {
			if (!Midi::is_midi_panel_id(m.panel_jack_id))
				continue;
			for (auto &j : m.ins) {
				if (j == jack)
					return &m;
			}
		}
		return nullptr;
	}

	const MappedInputJack *find_mapped_injack(Jack jack) const {
		for (auto &m : mapped_ins) {
			for (auto &j : m.ins) {
				if (j == jack)
					return &m;
			}
		}
		return nullptr;
	}

	const MappedInputJack *find_mapped_injack(uint32_t panel_jack_id) const {
		for (auto &m : mapped_ins) {
			if (m.panel_jack_id == panel_jack_id)
				return &m;
		}
		return nullptr;
	}

	const MappedOutputJack *find_mapped_outjack(Jack jack) const {
		for (auto &m : mapped_outs) {
			if (m.out == jack)
				return &m;
		}
		return nullptr;
	}

	const MappedOutputJack *find_mapped_outjack(uint32_t panel_jack_id) const {
		for (auto &m : mapped_outs) {
			if (m.panel_jack_id == panel_jack_id)
				return &m;
		}
		return nullptr;
	}

	void add_mapped_injack(uint32_t panel_jack_id, Jack jack) {
		for (auto &m : mapped_ins) {
			if (m.panel_jack_id == panel_jack_id) {
				for (auto &j : m.ins) {
					if (j == jack)
						return; // exact match already exists, no further action needed
				}
				m.ins.push_back(jack);
				update_midi_poly_num(panel_jack_id);
				return;
			}
		}
		mapped_ins.push_back({panel_jack_id, {jack}});
		update_midi_poly_num(panel_jack_id);
	}

	void add_mapped_outjack(uint32_t panel_jack_id, Jack jack) {
		mapped_outs.push_back({panel_jack_id, jack});
	}

	void set_panel_in_alias(uint32_t panel_jack_id, std::string_view alias) {
		for (auto &m : mapped_ins) {
			if (m.panel_jack_id == panel_jack_id) {
				m.alias_name.copy(alias);
			}
		}
	}

	void set_panel_out_alias(uint32_t panel_jack_id, std::string_view alias) {
		for (auto &m : mapped_outs) {
			if (m.panel_jack_id == panel_jack_id) {
				m.alias_name.copy(alias);
			}
		}
	}

	const InternalCable *find_internal_cable_with_outjack(Jack out_jack) const {
		for (auto &c : int_cables) {
			if (c.out == out_jack && c.ins.size() > 0) {
				return &c;
			}
		}
		return nullptr;
	}

	const InternalCable *find_internal_cable_with_injack(Jack in_jack) const {
		for (auto &c : int_cables) {
			for (auto &in : c.ins) {
				if (in == in_jack) {
					return &c;
				}
			}
		}
		return nullptr;
	}

	void trim_empty_knobsets() {
		if (knob_sets.size() == 0) {
			knob_sets.push_back({{}, "Knob Set 1"});
			return;
		}
		std::erase_if(knob_sets,
					  [this](auto const &knobset) { return knobset.set.size() == 0 && &knobset != &knob_sets[0]; });
	}

	const char *valid_knob_set_name(unsigned set_i) const {
		if (set_i == MIDIKnobSet)
			return "MIDI";

		if (set_i >= knob_sets.size())
			return "";

		if (knob_sets[set_i].name.length() > 0)
			return knob_sets[set_i].name.c_str();
		else
			return default_knob_set_name[set_i];
	}

	const char *default_knob_set_name[MaxKnobSets] = {
		"Knob Set 1",
		"Knob Set 2",
		"Knob Set 3",
		"Knob Set 4",
		"Knob Set 5",
		"Knob Set 6",
		"Knob Set 7",
		"Knob Set 8",
	};

	size_t add_module(std::string_view slug) {
		auto module_id = module_slugs.size();
		module_slugs.push_back({slug});
		// The new module has no measured load, so the balance must be re-calculated
		clear_load_balance();
		return module_id;
	}

	bool is_module_bypassed(uint16_t module_id) const {
		return std::find(bypassed_modules.begin(), bypassed_modules.end(), module_id) != bypassed_modules.end();
	}

	void set_module_bypassed(uint16_t module_id, bool bypassed) {
		auto it = std::find(bypassed_modules.begin(), bypassed_modules.end(), module_id);
		if (bypassed) {
			if (it == bypassed_modules.end())
				bypassed_modules.push_back(module_id);
		} else {
			if (it != bypassed_modules.end())
				bypassed_modules.erase(it);
		}
	}

	std::string_view get_module_alias(uint16_t module_id) const {
		for (auto const &a : module_aliases) {
			if (a.module_id == module_id)
				return a.alias_name.c_str();
		}
		return {};
	}

	void set_module_alias(uint16_t module_id, std::string_view alias) {
		auto it = std::find_if(
			module_aliases.begin(), module_aliases.end(), [=](auto const &a) { return a.module_id == module_id; });
		if (it != module_aliases.end()) {
			if (alias.empty())
				module_aliases.erase(it);
			else
				it->alias_name.copy(alias);
		} else if (!alias.empty()) {
			ModuleAlias new_alias;
			new_alias.module_id = module_id;
			new_alias.alias_name.copy(alias);
			module_aliases.push_back(new_alias);
		}
	}

	void remove_module(unsigned module_id) {
		if (module_id >= module_slugs.size())
			return;

		blank_out_module(module_id);

		module_slugs.erase(std::next(module_slugs.begin(), module_id));

		// Squash all module ids down:

		std::transform(int_cables.begin(), int_cables.end(), int_cables.begin(), [=](InternalCable &cable) {
			if (cable.out.module_id > module_id) {
				cable.out.module_id--;
			}

			std::transform(cable.ins.begin(), cable.ins.end(), cable.ins.begin(), [=](Jack &jack) {
				if (jack.module_id > module_id) {
					jack.module_id--;
				}
				return jack;
			});

			return cable;
		});

		for (auto &map : mapped_ins) {
			std::transform(map.ins.begin(), map.ins.end(), map.ins.begin(), [=](Jack &in) {
				if (in.module_id > module_id) {
					in.module_id--;
				}
				return in;
			});
		}

		std::transform(mapped_outs.begin(), mapped_outs.end(), mapped_outs.begin(), [=](MappedOutputJack &map) {
			if (map.out.module_id > module_id) {
				map.out.module_id--;
			}
			return map;
		});

		std::transform(static_knobs.begin(), static_knobs.end(), static_knobs.begin(), [=](StaticParam &map) {
			if (map.module_id > module_id) {
				map.module_id--;
			}
			return map;
		});

		for (auto &knobset : knob_sets) {
			std::transform(knobset.set.begin(), knobset.set.end(), knobset.set.begin(), [=](MappedKnob &map) {
				if (map.module_id > module_id) {
					map.module_id--;
				}
				return map;
			});
		}

		std::transform(midi_maps.set.begin(), midi_maps.set.end(), midi_maps.set.begin(), [=](MappedKnob &map) {
			if (map.module_id > module_id) {
				map.module_id--;
			}
			return map;
		});

		std::transform(module_states.begin(), module_states.end(), module_states.begin(), [=](ModuleInitState &state) {
			if (state.module_id > module_id) {
				state.module_id--;
			}
			return state;
		});

		for (auto &id : bypassed_modules) {
			if (id > module_id)
				id--;
		}

		for (auto &a : module_aliases) {
			if (a.module_id > module_id)
				a.module_id--;
		}

		for (auto &exp : expanders) {
			if (exp.left_module_id > module_id)
				exp.left_module_id--;
			if (exp.right_module_id > module_id)
				exp.right_module_id--;
		}

		std::erase_if(module_positions, [=](ModulePosition const &pos) { return pos.module_id == module_id; });
		for (auto &pos : module_positions) {
			if (pos.module_id > module_id)
				pos.module_id--;
		}
	}

	// Removes all cables, mappings, etc for a module
	// Except: keeps the slug in position
	void blank_out_module(unsigned module_id) {
		module_slugs[module_id] = "Blank";

		std::erase_if(int_cables, [=](InternalCable &cable) {
			if (cable.out.module_id == module_id) {
				return true;
			} else {
				std::erase_if(cable.ins, [=](Jack const &in) { return in.module_id == module_id; });
				return (cable.ins.size() == 0);
			}
		});

		for (MappedInputJack &map : mapped_ins) {
			std::erase_if(map.ins, [=](Jack const &in) { return in.module_id == module_id; });
		}
		std::erase_if(mapped_ins, [=](MappedInputJack const &map) { return map.ins.size() == 0; });
		std::erase_if(mapped_outs, [=](MappedOutputJack const &map) { return map.out.module_id == module_id; });

		std::erase_if(static_knobs, [=](StaticParam const &knob) { return knob.module_id == module_id; });

		for (auto &knobset : knob_sets) {
			std::erase_if(knobset.set, [=](MappedKnob const &map) { return map.module_id == module_id; });
		}
		std::erase_if(midi_maps.set, [=](MappedKnob const &map) { return map.module_id == module_id; });

		std::erase_if(module_states, [=](ModuleInitState const &state) { return state.module_id == module_id; });

		std::erase(bypassed_modules, static_cast<uint16_t>(module_id));

		std::erase_if(module_aliases, [=](ModuleAlias const &a) { return a.module_id == module_id; });

		remove_expanders(module_id);

		// The module's measured load no longer applies
		clear_load_balance();
	}

	//
	// Expanders
	//

	bool add_expander(ExpanderConnection conn) {
		if (can_add_expander(conn)) {
			expanders.push_back(conn);
			return true;
		}
		return false;
	}

	// True if the connection is legal
	bool can_add_expander(ExpanderConnection conn) const {
		auto num_modules = module_slugs.size();
		if (conn.left_module_id == conn.right_module_id)
			return false;
		if (conn.left_module_id == 0 || conn.right_module_id == 0)
			return false;
		if (conn.left_module_id >= num_modules || conn.right_module_id >= num_modules)
			return false;
		if (find_expander(conn.left_module_id, ExpanderSide::Right))
			return false;
		if (find_expander(conn.right_module_id, ExpanderSide::Left))
			return false;
		// Avoid loops
		if (expander_chain_reaches(conn.right_module_id, ExpanderSide::Right, conn.left_module_id))
			return false;
		return true;
	}

	std::optional<ExpanderConnection> find_expander(uint16_t module_id, ExpanderSide side) const {
		for (auto const &exp : expanders) {
			if (side == ExpanderSide::Left && exp.right_module_id == module_id)
				return exp;
			if (side == ExpanderSide::Right && exp.left_module_id == module_id)
				return exp;
		}
		return std::nullopt;
	}

	std::optional<uint16_t> find_expander_module(uint16_t module_id, ExpanderSide side) const {
		if (auto exp = find_expander(module_id, side))
			return side == ExpanderSide::Left ? exp->left_module_id : exp->right_module_id;
		return std::nullopt;
	}

	// True if target_module_id is reachable from module_id by following expander
	// connections on the given side
	bool expander_chain_reaches(uint16_t module_id, ExpanderSide side, uint16_t target_module_id) const {
		auto max_steps = module_slugs.size();
		auto cur = module_id;
		for (size_t i = 0; i < max_steps; i++) {
			auto next = find_expander_module(cur, side);
			if (!next)
				return false;
			if (*next == target_module_id)
				return true;
			cur = *next;
		}
		return false;
	}

	bool has_expander(ExpanderConnection conn) const {
		return std::ranges::any_of(expanders, [=](ExpanderConnection const &exp) {
			return exp.left_module_id == conn.left_module_id && exp.right_module_id == conn.right_module_id;
		});
	}

	// Removes all expander connections to or from this module
	void remove_expanders(uint16_t module_id) {
		std::erase_if(expanders, [=](ExpanderConnection const &exp) {
			return exp.left_module_id == module_id || exp.right_module_id == module_id;
		});
	}

	bool remove_expander(ExpanderConnection conn) {
		auto erased = std::erase_if(expanders, [=](ExpanderConnection const &exp) {
			return exp.left_module_id == conn.left_module_id && exp.right_module_id == conn.right_module_id;
		});
		return erased > 0;
	}

private:
	//non-const version for private use only
	MappedKnob *_get_mapped_knob(uint32_t set_id, uint32_t module_id, uint32_t param_id) {
		if (set_id < knob_sets.size()) {
			for (auto &m : knob_sets[set_id].set) {
				if (m.module_id == module_id && m.param_id == param_id)
					return &m;
			}
		}
		return nullptr;
	}

	MappedKnob *_get_midi_map(uint32_t module_id, uint32_t param_id) {
		for (auto &m : midi_maps.set) {
			if (m.module_id == module_id && m.param_id == param_id)
				return &m;
		}
		return nullptr;
	}

	InternalCable *_find_internal_cable_with_outjack(Jack out_jack) {
		for (auto &c : int_cables) {
			if (c.out == out_jack && c.ins.size() > 0) {
				return &c;
			}
		}
		return nullptr;
	}

	void update_midi_poly_num(uint32_t panel_jack_id) {
		// User hard-set the count: ignore cables
		if (midi_poly_num_setting > 0) {
			midi_poly_num = midi_poly_num_setting;
			return;
		}

		if (Midi::is_midi_poly5_8_cable(panel_jack_id)) {
			// 5-8 cable carries poly channels 5-8, so it needs the full polyphony allocated
			midi_poly_num = std::max<uint32_t>(MaxMidiPolyphony, midi_poly_num);
		} else if (Midi::is_midi_poly_cable(panel_jack_id)) {
			midi_poly_num = std::max<uint32_t>(MaxMidiPolyChannels, midi_poly_num);
		} else if (auto poly = Midi::polychan(panel_jack_id)) {
			midi_poly_num = std::max<uint32_t>(*poly, midi_poly_num);
		}
	}
};

} // namespace MetaModule
