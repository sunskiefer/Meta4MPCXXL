#include "pages.hh"
#include "CoreModules/moduleFactory.hh"
#include "gui/elements/state_names.hh"
#include "patch_play/patch_player.hh"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace MetaModule;

namespace Meta4ms
{

namespace
{

std::string upper(std::string_view s) {
	std::string r{s};
	for (auto &c : r)
		c = (char)std::toupper((unsigned char)c);
	return r;
}

std::string trim_to(std::string s, size_t n) {
	if (s.size() > n)
		s.resize(n);
	while (!s.empty() && s.back() == ' ')
		s.pop_back();
	return s;
}

std::string element_name(Element const &el) {
	auto b = base_element(el);
	std::string_view n = b.long_name.size() ? b.long_name : b.short_name;
	return std::string{n};
}

// Number of discrete positions of a parameter element (0 = continuous)
unsigned element_positions(Element const &el, bool *button) {
	*button = false;
	unsigned n = 0;
	std::visit(overloaded{
				   [&](FlipSwitch const &e) { n = e.num_pos; },
				   [&](SlideSwitch const &e) { n = e.num_pos; },
				   [&](KnobSnapped const &e) { n = (e.num_pos > 1 && e.num_pos < 1000) ? e.num_pos : 0; },
				   [&](AltParamChoice const &e) { n = e.num_pos; },
				   [&](AltParamChoiceLabeled const &e) { n = e.num_pos; },
				   [&](MomentaryButton const &) { *button = true; },
				   [&](LatchingButton const &) { *button = true; },
				   [](BaseElement const &) {},
			   },
			   el);
	return n > 1 ? n : 0;
}

// Elements that are parameters a person sets: knobs, sliders, switches, buttons, hidden menu choices.
// Encoders (relative), action items and the like are left out.
bool is_settable(Element const &el) {
	bool ok = false;
	std::visit(overloaded{
				   [&](Encoder const &) { ok = false; },
				   [&](AltParamAction const &) { ok = false; },
				   [&](ParamElement const &) { ok = true; },
				   [&](AltParamContinuous const &) { ok = true; },
				   [&](AltParamChoice const &) { ok = true; },
				   [](BaseElement const &) {},
			   },
			   el);
	return ok;
}

const char *knob_letter(unsigned id) {
	static const char *names[] = {"A", "B", "C", "D", "E", "F", "U", "V", "W", "X", "Y", "Z"};
	return id < 12 ? names[id] : "";
}

struct ParamInfo {
	const Element *el = nullptr;
	std::string name;
};

ParamInfo find_param(std::string_view slug, unsigned param_id) {
	auto &info = ModuleFactory::getModuleInfo(slug);
	for (size_t i = 0; i < info.elements.size() && i < info.indices.size(); i++) {
		if (info.indices[i].param_idx == param_id && info.indices[i].param_idx != ElementCount::Indices::NoElementMarker)
			return {&info.elements[i], element_name(info.elements[i])};
	}
	return {};
}

void add_paged(std::vector<Page> &out, std::string const &title, std::vector<Item> &items) {
	if (items.empty())
		return;
	unsigned n = (unsigned)((items.size() + SlotsPerPage - 1) / SlotsPerPage);
	for (unsigned p = 0; p < n; p++) {
		Page pg;
		pg.title = trim_to(title, n > 1 ? 15 : 19);
		if (n > 1)
			pg.title += " " + std::to_string(p + 1) + "/" + std::to_string(n);
		for (unsigned i = p * SlotsPerPage; i < std::min<size_t>(items.size(), (p + 1) * SlotsPerPage); i++)
			pg.items.push_back(items[i]);
		out.push_back(std::move(pg));
	}
}

} // namespace

std::string module_title(PatchData const &pd, unsigned module_id) {
	if (module_id >= pd.module_slugs.size())
		return "";
	for (auto &a : pd.module_aliases)
		if (a.module_id == module_id && a.alias_name.length())
			return upper(std::string_view{a.alias_name});

	std::string_view slug = pd.module_slugs[module_id];
	std::string name{ModuleFactory::getModuleDisplayName(slug)};
	if (name.empty()) {
		auto colon = slug.find(':');
		name = std::string{colon == std::string_view::npos ? slug : slug.substr(colon + 1)};
	}
	// "#2" for the second of two identical modules, as MetaModule labels them
	unsigned count = 0, index = 0;
	for (unsigned i = 1; i < pd.module_slugs.size(); i++) {
		if (std::string_view{pd.module_slugs[i]} == slug) {
			count++;
			if (i == module_id)
				index = count;
		}
	}
	name = upper(name);
	if (count > 1)
		name = trim_to(name, 15) + " #" + std::to_string(index);
	return name;
}

void Pages::build(PatchData const &pd, PatchPlayer &player) {
	pages.clear();
	knob_set_pages = 0;

	// 1. knob sets
	for (unsigned ks = 0; ks < pd.knob_sets.size(); ks++) {
		auto const &set = pd.knob_sets[ks];
		std::vector<Item> items;
		std::map<uint16_t, size_t> by_knob;
		for (auto const &m : set.set) {
			if (m.module_id >= pd.module_slugs.size())
				continue;
			auto it = by_knob.find(m.panel_knob_id);
			if (it != by_knob.end()) {
				items[it->second].maps.push_back(m);
				continue;
			}
			Item item;
			item.kind = Item::Kind::KnobSetKnob;
			item.knob_set = ks;
			item.panel_knob = m.panel_knob_id;
			item.module_id = m.module_id;
			item.param_id = m.param_id;
			item.maps.push_back(m);
			auto pi = find_param(pd.module_slugs[m.module_id], m.param_id);
			item.element = pi.el;
			if (pi.el)
				item.positions = element_positions(*pi.el, &item.button);
			std::string label = m.alias_name.length() ? std::string{std::string_view{m.alias_name}} : pi.name;
			if (label.empty())
				label = std::string{"KNOB "} + knob_letter(m.panel_knob_id);
			item.name = trim_to(upper(label), 16);
			// the knob's own position: where its first map's parameter sits now
			float pv = player.get_param(m.module_id, m.param_id);
			item.knob_pos = std::clamp(m.unmap_val(pv), 0.f, 1.f);
			by_knob[m.panel_knob_id] = items.size();
			items.push_back(std::move(item));
		}
		std::sort(items.begin(), items.end(), [](Item const &a, Item const &b) { return a.panel_knob < b.panel_knob; });
		std::string title = set.name.length() ? upper(std::string_view{set.name}) : "KNOB SET " + std::to_string(ks + 1);
		size_t before = pages.size();
		add_paged(pages, title, items);
		knob_set_pages += (unsigned)(pages.size() - before);
	}

	// 2. every module's parameters (module 0 is the hub: its knobs are the knob sets above)
	for (unsigned mid = 1; mid < pd.module_slugs.size(); mid++) {
		std::string_view slug = pd.module_slugs[mid];
		if (!ModuleFactory::isValidSlug(slug))
			continue;
		auto &info = ModuleFactory::getModuleInfo(slug);
		std::vector<Item> items;
		for (size_t i = 0; i < info.elements.size() && i < info.indices.size(); i++) {
			auto pidx = info.indices[i].param_idx;
			if (pidx == ElementCount::Indices::NoElementMarker)
				continue;
			auto const &el = info.elements[i];
			if (!is_settable(el))
				continue;
			Item item;
			item.kind = Item::Kind::Param;
			item.module_id = (uint16_t)mid;
			item.param_id = pidx;
			item.element = &el;
			item.positions = element_positions(el, &item.button);
			std::string label = element_name(el);
			if (label.empty())
				label = "PARAM " + std::to_string(pidx + 1);
			item.name = trim_to(upper(label), 16);
			items.push_back(std::move(item));
		}
		add_paged(pages, module_title(pd, mid), items);
	}
}

float Pages::get(Item const &it, PatchPlayer &player) {
	if (it.kind == Item::Kind::KnobSetKnob)
		return it.knob_pos;
	return std::clamp(player.get_param(it.module_id, it.param_id), 0.f, 1.f);
}

static float snap(Item const &it, float v) {
	v = std::clamp(v, 0.f, 1.f);
	if (it.button)
		return v >= 0.5f ? 1.f : 0.f;
	if (it.positions > 1)
		return std::round(v * (float)(it.positions - 1)) / (float)(it.positions - 1);
	return v;
}

void Pages::set(Item &it, float v, PatchPlayer &player) {
	v = std::clamp(v, 0.f, 1.f);
	if (it.kind == Item::Kind::KnobSetKnob) {
		it.knob_pos = v;
		for (auto const &m : it.maps) {
			float mv;
			if (m.curve_type == MappedKnob::Toggle)
				mv = v >= 0.5f ? m.max : m.min;
			else
				mv = m.get_mapped_val(v);
			player.apply_static_param(StaticParam{m.module_id, m.param_id, mv});
		}
		return;
	}
	player.apply_static_param(StaticParam{it.module_id, it.param_id, snap(it, v)});
}

std::string Pages::value_text(Item const &it, PatchPlayer &player) {
	float v = std::clamp(player.get_param(it.module_id, it.param_id), 0.f, 1.f);
	if (!it.element)
		return std::to_string((int)std::lround(v * 100.f)) + "%";
	auto s = get_element_value_string(*it.element, v, 100.f);
	// 4ms prints custom-range values with up to five decimals ("36.958%"): three significant digits read better
	// on the Force ("37%", "1.21s", "440Hz"); text values ("Saw") are left alone
	if (!s.empty() && (std::isdigit((unsigned char)s[0]) || s[0] == '-' || s[0] == '.')) {
		char *end = nullptr;
		double num = std::strtod(s.c_str(), &end);
		if (end && end != s.c_str()) {
			char b[32];
			std::snprintf(b, sizeof b, "%.3g", num);
			std::string unit = end;
			s = std::string(b) + unit;
		}
	}
	if (s.empty())
		s = std::to_string((int)std::lround(v * 100.f)) + "%";
	if (s.size() > 22)
		s.resize(22);
	return s;
}

} // namespace Meta4ms
