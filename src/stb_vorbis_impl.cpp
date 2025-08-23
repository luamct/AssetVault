// This file provides the stb_vorbis implementation
// It must be compiled separately to avoid multiple definition errors

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4701) // potentially uninitialized local variable
#endif

// miniaudio needs the full stb_vorbis API
#include "miniaudio/stb_vorbis.c"

#ifdef _MSC_VER
#pragma warning(pop)
#endif
