# Bounded socket callsite discovery

This experimental diagnostic is off unless `PS3_NET_PROBE_ARM_SCRIPT` is a valid zero-based PAD_SCRIPT index0..255.
It does not add settings traffic, readiness ACKs, packet prefixes, waiting gates or player menu options.
`PS3_NET_PROBE_DURATION_MS` defaults to60000 (valid1..600000); `PS3_NET_PROBE_BUDGET` defaults to4096
(valid1..65536). Missing/invalid configuration leaves the ordinary handlers and input observer unchanged.
Configure once before guest threads; live reconfiguration/rearming is unsupported.

The optional observer is defined null in `cellPad.c`, so standalone input code has no network link dependency.
The opted-in port registers it plus a new CheckCallback wrapper from `stubs.cpp` before ordinary HLE startup.
The selected ordinary PAD_SCRIPT event must still be active on the current call, survive port filters and appear
in a nonempty successfully returned report after guest serialization. A len0 drain, filtered/zero/high-bit mask,
wrong port/index or nonordinary script kind cannot arm this recorder. Expired holds are not extended or resurrected.
The marker is `SCRIPT_REPORT_PRESENT`, not a button edge or proof of UI consumption. Existing script lazy/shared
state assumptions remain; control the input-owner fixture before treating phase correlation as reliable.

One marker starts one bounded window. Script and socket API observations use the same GetTickCount64 millisecond
clock abstraction. Its absolute monotonic epoch differs from `[flip] ... at ... ms`, which starts at its own first
flip. Captures must independently show the intended Go-to-battle transition. A wrong-screen press is not discovery.

The four actual online NID wrappers record raw64-bit thread ID/LR, operation, fd slot, requested length/flags,
raw encoded int32 API result and begin/end times. They invoke the original core exactly once with the same args;
errors retain the existing `0x80010200 | BSD_errno` encoding and sign extension. Enabled bookkeeping preserves
CRT and platform error state. Default-off/unarmed/closed calls take the original call/return path without clocks,
allocation, ring locks or TLS lookup. There are no payload, endpoint, guest-buffer pointer, account or room copies.

A socket API return is not packet delivery. sendto(null) delegates internally but produces only one wrapper record;
relay routing can return requested length even when its underlying sender fails. recvfrom can consume probes/punches
and unwrap traffic before a title-visible return; stream operations can return partial bytes or0. Empty encoded
receive-wouldblock returns are counted separately rather than consuming record budget. No transport route, peer
identity, complete wire capture or semantic battle-start conclusion follows. `registration_entry_seen` only observes
entry to host registration; first-wins lookup/capacity and actual selected-handler coverage remain separate.

Entry tickets prevent a pre-arm blocked call from becoming a post-arm observation. Admitted calls returning after
the deadline remain accounted as late; capacity/budget/contention losses are explicit. A call that stays blocked has
no return record. Fixed256-record storage publishes/copies under one nonblocking try-lock and never overwrites
older records. Accepted return records also have a finite window budget. Queue and scalar counters are individually
safe, but live snapshots are not coherent final accounting.

The new port wrapper calls the existing C `cellSysutilCheckCallback` directly once, preserving its result and normal
callback/UDP progress. It never redispatches its own NID. Only the outermost invocation tries to own the consumer,
then copies at most32 records. Formatting/flush occur outside runtime/ring locks, with explicit IO failure state.
Synchronous output perturbs the polling thread: do not use trace-on sessions to claim FPS/input-latency improvement.
Every output snapshot says `completeness=unknown`; forced harness termination, missing pumps/terminal drain and
outstanding calls cannot establish complete absence. Keep legacy `PS3_NET_TRACE` unset: its current presence test
also treats value0 as on. Other unrelated traces should be disabled in the focused fixture.

Developer validation (native commands require the shared build lock and an appropriate existing Nix shell):

```sh
python3 installer/tools/export_toolkit.py --out "$SDK" --repo "$TOOLKIT_REPO" --no-fetch
python3 installer/tools/net_probe_test.py --sdk "$SDK" --sdl-config "$SDL_CONFIG" --mutations
python3 installer/tools/net_probe_consumer_test.py --sdk "$SDK" --mutations
python3 installer/tools/net_probe_core_test.py --sdk "$SDK"
```

The first maintained suite compiles actual recorder/wrapper bodies with controlled core/error/time adapters and the
full authored cellPad implementation with fake devices and serialized guest reports. The consumer suite compiles
the actual port consumer plus recorder with synthetic callback/context/error adapters. The core suite executes the
existing actual sysNet/NID loopback fixture with capture off/armed, including its real C cores, null-sendto fallback
and a controlled route-stub early return. That route stub is not relay delivery evidence. Windows error adapters
are synthetic; actual Windows-target syntax/build/title checks are distinct. Source-specific receipts and resolved
refutation findings accompany the exact local checkpoint; no executable or online acceptance is inferred here.
