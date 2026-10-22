#pragma once
#include "ryml.hpp"

namespace RymlInit
{
void init_once();

// Parse a yaml or json buffer into tree. Returns false if it's malformed, and then tree
// must not be used. tree must have been created after init_once() was called.
bool parse_in_place(c4::substr src, ryml::Tree *tree);
bool parse_in_arena(c4::csubstr src, ryml::Tree *tree);
} // namespace RymlInit
