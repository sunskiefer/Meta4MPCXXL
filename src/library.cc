#include "library.hh"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace Meta4MPC
{

namespace
{

constexpr int MaxDepth = 4;
constexpr size_t MaxEntries = 2000;

bool has_patch_ext(std::string const &n) {
	auto dot = n.rfind('.');
	if (dot == std::string::npos)
		return false;
	std::string ext = n.substr(dot + 1);
	for (auto &c : ext)
		c = (char)std::tolower((unsigned char)c);
	return ext == "yml" || ext == "yaml";
}

void walk(std::string const &root, std::string const &sub, int depth, bool factory, std::vector<PatchEntry> &out) {
	if (depth > MaxDepth || out.size() >= MaxEntries)
		return;
	std::string dir = sub.empty() ? root : root + "/" + sub;
	DIR *d = opendir(dir.c_str());
	if (!d)
		return;
	std::vector<std::string> names;
	while (auto *e = readdir(d)) {
		if (e->d_name[0] == '.')
			continue;
		names.emplace_back(e->d_name);
	}
	closedir(d);
	std::sort(names.begin(), names.end());

	std::vector<std::string> dirs;
	for (auto &n : names) {
		std::string full = dir + "/" + n;
		struct stat st;
		if (lstat(full.c_str(), &st) != 0 || S_ISLNK(st.st_mode))
			continue;
		std::string rel = sub.empty() ? n : sub + "/" + n;
		if (S_ISDIR(st.st_mode)) {
			dirs.push_back(rel);
		} else if (S_ISREG(st.st_mode) && has_patch_ext(n) && st.st_size > 0 && st.st_size < 4 * 1024 * 1024) {
			PatchEntry pe;
			pe.path = full;
			pe.rel = rel;
			std::string stem = n.substr(0, n.rfind('.'));
			for (auto &c : stem)
				c = (char)std::toupper((unsigned char)c);
			pe.label = stem;
			pe.factory = factory;
			out.push_back(std::move(pe));
		}
	}
	for (auto &r : dirs)
		walk(root, r, depth + 1, factory, out);
}

} // namespace

std::vector<PatchEntry> scan_library(std::string const &factory_dir) {
	std::vector<PatchEntry> out;
	if (!factory_dir.empty())
		walk(factory_dir, "", 1, true, out);

	std::vector<std::string> drives;
	std::string mroot = media_root();
	if (DIR *d = opendir(mroot.c_str())) {
		while (auto *e = readdir(d)) {
			if (e->d_name[0] != '.')
				drives.emplace_back(e->d_name);
		}
		closedir(d);
	}
	std::sort(drives.begin(), drives.end());
	for (auto &drv : drives) {
		std::string root = mroot + "/" + drv + "/Meta4MPCXXL";
		struct stat st;
		if (stat(root.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
			walk(root, "", 1, false, out);
	}
	return out;
}

int find_entry(std::vector<PatchEntry> const &lib, std::string const &rel, std::string const &label) {
	for (size_t i = 0; i < lib.size(); i++)
		if (!rel.empty() && lib[i].rel == rel)
			return (int)i;
	for (size_t i = 0; i < lib.size(); i++)
		if (!label.empty() && lib[i].label == label)
			return (int)i;
	return -1;
}

std::string media_root() {
	const char *e = std::getenv("META4MPCXXL_MEDIA_ROOT");
	return e && *e ? e : "/media";
}

std::string read_file(std::string const &path) {
	std::string s;
	FILE *f = fopen(path.c_str(), "rb");
	if (!f)
		return s;
	char buf[16384];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0)
		s.append(buf, n);
	fclose(f);
	return s;
}

} // namespace Meta4MPC
