#include "math_lut.hh"
#include <cmath>

struct Pow2TableRange {
	static constexpr float min = -5.1f;
	static constexpr float max = 6.1f;
};

Mapping::LookupTable_t<64, float> Pow2 =
	Mapping::LookupTable_t<64, float>::generate<Pow2TableRange>([](auto x) { return std::pow(2.f, x); });


struct TanfTableRange {
	static constexpr float min = 0.0004f;
	static constexpr float max = 3.69f;
};
Mapping::LookupTable_t<64, float> Tanf =
	Mapping::LookupTable_t<64, float>::generate<TanfTableRange>([](auto x) { return std::tan(x); });
