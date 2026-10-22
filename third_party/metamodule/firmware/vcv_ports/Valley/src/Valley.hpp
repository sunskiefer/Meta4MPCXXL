// On x86, native SSE headers must be included before rack.hpp enables simde's _mm_* aliases
#if defined(SIMULATOR) || defined(__SSE2__) || defined(_M_X64)
#include "simd/SIMDUtilities.hpp"
#endif

#include "rack.hpp"
#define VALLEY_VERSION 204
#define DR_WAV_IMPLEMENTATION
using namespace rack;

// #include <iomanip> // setprecision
// #include <sstream> // stringstream
#define VALLEY_NAME "Valley"

#define TFORM_MAX_BANKS 64
#define TFORM_MAX_NUM_WAVES 64
#define TFORM_MAX_WAVELENGTH 256
#define TFORM_WAVELENGTH_CAP 256

extern Plugin *pluginInstance;
extern Plugin *valleyPluginInstance;

////////////////////
// module widgets
////////////////////

extern Model *modelTopograph;
extern Model *modelUGraph;
extern Model *modelDexter;
extern Model *modelPlateau;
extern Model *modelInterzone;
extern Model *modelAmalgam;
extern Model *modelFeline;
extern Model *modelTerrorform;
