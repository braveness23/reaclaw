// Compile SWELL function pointer table for this extension.
//
// SWELL_PROVIDED_BY_APP tells swell-modstub-generic.cpp to declare all SWELL
// function pointer globals and provide SWELL_dllMain to populate them from the
// host application (REAPER) via GetFunc at runtime.  No GTK headers needed.
//
// This file must be compiled exactly once per extension binary.
#ifndef _WIN32

#ifdef __APPLE__
// Pre-include C++ headers that swell.h transitively pulls in via swell-types.h.
// On Xcode 15+, libc++ uses C++ templates internally in <cstddef>. Those
// templates cannot have C linkage, so including them inside the extern "C" {}
// block in swell-modstub-generic.cpp causes compile errors. Pre-including here
// means the include guards fire before swell-modstub-generic.cpp gets to them.
#include <cstddef>
#include <cstdint>
#endif

#define SWELL_PROVIDED_BY_APP
#include "WDL/swell/swell-modstub-generic.cpp"
#endif
