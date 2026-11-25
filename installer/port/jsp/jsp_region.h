// jsp_region.h -- which disc's code this exe is lifted from, the one place the port asks.
//
// The port's hooks, guest data addresses and direct calls of lifted functions (func_XXXXXXXX) are those of the US 1.00 EBOOT
// (BLUS31519): the EU EBOOT (BLES02136) has the same program at other addresses (function entries move by -4244 to +1452 bytes,
// data past about 0x8d8000 by +8 to +24; the project's binary notes), and most US function names do not exist in the EU lift at all.
// So every feature that needs them is US only for now, at two levels:
//   - compile time: JSP_US_CODE is 1 only in a US build. CMake sets JSP_REGION_EU=1 when firstrun.py passes JSP_GAME_REGION=EU;
//     code that calls a US-only lifted function sits in `#if JSP_US_CODE` (an EU lift does not declare it), with an EU branch that
//     keeps every symbol other files link to. tools/ci/port_syntax_check.py compiles every port source both ways.
//   - run time: jsp_game_is_us() is false in an EU build; each US-only feature asks it before it installs a hook or writes guest
//     memory, and logs that it is "not available on this disc yet".
// No EU address is guessed anywhere: an EU feature needs its own addresses, read from the EU lift.
#pragma once

#if defined(JSP_REGION_EU) && JSP_REGION_EU
#define JSP_US_CODE 0
#else
#define JSP_US_CODE 1   // a US build, or a build without a region (host tests, the US-only syntax pass): the port as before
#endif

static inline bool jsp_game_is_us(void) { return JSP_US_CODE != 0; }

// The log text a US-only feature prints on another disc (the [portmenu], [unlock], [mods], [net] ... lines).
#define JSP_NOT_THIS_DISC "not available on this disc yet (US disc BLUS31519 only for now)"
