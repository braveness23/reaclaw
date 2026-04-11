// Compile SWELL function pointer table for this extension.
//
// SWELL_PROVIDED_BY_APP tells swell-modstub-generic.cpp to declare all SWELL
// function pointer globals and provide SWELL_dllMain to populate them from the
// host application (REAPER) via GetFunc at runtime.  No GTK headers needed.
//
// This file must be compiled exactly once per extension binary.
#ifndef _WIN32
#define SWELL_PROVIDED_BY_APP
#include "WDL/swell/swell-modstub-generic.cpp"
#endif
