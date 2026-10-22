#include "ryml_init.hh"
#include <cstdio>

namespace RymlInit
{

#if defined(LOG_LEVEL) && (LOG_LEVEL > 2)
#define ryml_printf printf
#else
#define ryml_printf(...)
#endif

namespace
{
struct ParseError {};

bool parsing = false;

} // namespace

// Trees and parsers copy the global callbacks when they're created, so this must be
// called before creating any
void init_once() {
	static bool already_init = false;
	static c4::yml::Callbacks callbacks;
	if (!already_init) {
		already_init = true;

		// ryml expects its error handler to never return. Parse errors throw, but errors from
		// reading a tree (e.g. missing key) don't throw in our read() overloads.
		callbacks.m_error = [](const char *msg, size_t /*msg_len*/, c4::yml::Location loc, void * /*user_data*/) {
			if (loc.name.empty())
				ryml_printf("[ryml] %s\n", msg);
			else
				ryml_printf("[ryml] %s in %s %zu:%zu)\n", msg, loc.name.data(), loc.line, loc.col);

			if (parsing)
				throw ParseError{};
		};

		c4::yml::set_callbacks(callbacks);
		c4::set_error_flags(c4::ON_ERROR_DEBUGBREAK | c4::ON_ERROR_LOG);
	}
}

template<typename F>
static bool parse_with(F &&parse) {
	init_once();

	parsing = true;
	bool ok = true;
	try {
		parse();
	} catch (ParseError const &) {
		ok = false;
	}
	parsing = false;

	return ok;
}

bool parse_in_place(c4::substr src, ryml::Tree *tree) {
	return parse_with([&] { ryml::parse_in_place(src, tree); });
}

bool parse_in_arena(c4::csubstr src, ryml::Tree *tree) {
	return parse_with([&] { ryml::parse_in_arena(src, tree); });
}

} // namespace RymlInit
