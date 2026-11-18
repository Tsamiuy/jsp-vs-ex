// jsp_hook_chain.h: hook chaining for the port's ppu_register_function wrappers.
// The toolkit registry keeps ONE function per guest address, and the wrappers used to read their `orig` from function_table
// (the lifted original), so whoever registered last on an address silently unhooked the others (for example two hooks on
// @0x0014EA68). Now every wrapper that can share an address installs through jsp_hook_chain::install_with(): `orig` = the
// function registered by the previous port hook for that address if there is one, else the lifted function from
// function_table. Install order = call order of the installers (stubs.cpp ps3_load_prx_modules: the fight hash; then main.cpp:
// the rollback stepper and session), so a later installer is the OUTER wrapper: when it skips the original, the inner hooks do
// not run either.
#pragma once
#include <cstdint>
#include <map>

namespace jsp_hook_chain {

inline std::map<uint32_t, void*>& registry() { static std::map<uint32_t, void*> m; return m; }

// Pure part (host-testable): find the function to chain to, register the wrapper, record it. Returns false if there is nothing
// to chain to (no earlier hook, no lifted function).
template <class Fn, class Lifted, class Register>
bool install_with(uint32_t addr, Fn wrapper, Fn* orig, Lifted lifted, Register reg)
{
    Fn prev = nullptr;
    auto it = registry().find(addr);
    if (it != registry().end()) prev = reinterpret_cast<Fn>(it->second);
    else prev = lifted(addr);
    if (!prev) return false;
    *orig = prev;
    reg(addr, wrapper);
    registry()[addr] = reinterpret_cast<void*>(wrapper);
    return true;
}

}  // namespace jsp_hook_chain


#ifndef JSP_HOOK_CHAIN_PURE
// Runtime flavour: needs ppu_recomp.h (function_table) and ppu_register_function declared by the includer.
namespace jsp_hook_chain {
template <class Fn>
inline bool install(uint32_t addr, Fn wrapper, Fn* orig)
{
    return install_with(addr, wrapper, orig,
        [](uint32_t a) -> Fn { for (uint64_t i = 0; i < function_table_count; i++) if (function_table[i].addr == a) return function_table[i].func; return nullptr; },
        [](uint32_t a, Fn w) { ppu_register_function(a, w); });
}
}  // namespace jsp_hook_chain
#endif
