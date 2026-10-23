// Meta4MPCXXL: the list of patch files the plugin can load.
//
// Scanned on the instance's worker thread, never on the audio thread:
//   1. the factory patches in the plugin's own folder (<plugin dir>/patches),
//   2. a folder named "Meta4MPCXXL" at the top of any drive under /media (the Force's internal storage, SD cards, USB
//      drives), so patches exported from VCV Rack are copied there and show up after a rescan.
// Up to 4 folder levels deep, no symlinks, sorted (factory first, then by folder, then by name), so a position in the
// list always means the same patch. The plugin state stores the chosen patch by its path relative to the folder it was
// found in, and by name, and finds it again by either when the list changes.
#pragma once
#include <string>
#include <vector>

namespace Meta4MPC
{

struct PatchEntry {
	std::string path;  // absolute
	std::string rel;   // relative to its library root, e.g. "Drums/QuadDrum.yml"
	std::string label; // shown on screen: the file name without .yml, upper case
	bool factory = false;
};

std::vector<PatchEntry> scan_library(std::string const &factory_dir);

// index of the entry matching a saved rel path (or, failing that, label); -1 when not found
int find_entry(std::vector<PatchEntry> const &lib, std::string const &rel, std::string const &label);

std::string read_file(std::string const &path);

// where drives are mounted: /media (META4MPCXXL_MEDIA_ROOT overrides it, for the offline tests)
std::string media_root();

} // namespace Meta4MPC
