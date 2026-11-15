#pragma once
#include <cstddef>
#include <cstdint>
#if __has_include(<jsp_lod_hooks_build.h>)
#include <jsp_lod_hooks_build.h>
#else
#define JSP_LOD_ENTRY_HOOKS_VERIFIED 0
#define JSP_LOD_PROGRAM_SHA256 ""
#define JSP_LOD_PROGRAM_SEGMENT_SHA256 ""
#endif

// Buffer is owned by the loader and is the exact input subsequently copied to
// guest memory. nullptr revokes the previous binding before any load attempt.
extern "C" int jsp_lod_bind_loaded_image(const uint8_t* buffer, size_t size);
extern "C" bool jsp_lod_hooks_active(void);
extern "C" void jsp_lod_lifetime_reset(void);
