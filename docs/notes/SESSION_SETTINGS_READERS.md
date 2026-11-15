# Temporary session inputs for player settings readers

This is integration plumbing, not working multiplayer settings synchronization. No title lifecycle, authenticated
transport, application acknowledgment or battle-start gate calls the new session API yet. Ordinary local settings
continue to work without creating a session.

The pure `jsp_session_settings.h` contract defines a conservative five-key profile: normal model detail, object
distance and shadow range, with PIPE and render60 off. Its policy is promoted unchanged; `jsp_session_reader.h`
connects it to the actual player settings store. A trusted local lifecycle must bind a fresh session identity before
installing a proposal. The caller must authenticate the sender and bind its roster/owner/revision; these fields are
not network authentication by themselves.

`reader_install` stages validation and the temporary layer before replacing the current state. Rejected proposals
leave the prior layer and generation intact. The three live values override ordinary local values in
`jsp_setting_get`; environment, local edits and preference files are not changed by a session installation.
Local menu edits underneath the layer remain available when the exact token is cleared. Clearing also ends the
binding so delayed proposals cannot reactivate it. Whole-session teardown is a trusted local operation.

Startup evidence is unavailable by default. A caller must supply evidence from actual initialized consumers;
reading environment variables or accepting a network field is insufficient. PIPE can be disabled by debug flags
or thread creation failure, so requested configuration is not its effective state. This player source contains no
render60 module; an explicit build-capability witness for that absence is different from a cached environment read.
`reader_startup()` now reads the actual initialized renderer decision through patch7904. Unknown producer state,
shutdown/reinit and unsupported ordering remain unavailable. The canonical CMake target explicitly declares
render60 absent and rejects the known module path until that declaration is reviewed. This is a trusted build
contract, not arbitrary-mod detection. Other targets/unattested builds stay unavailable. The two startup slots
alone carry observations; live slots are not application read-back. This point read does not pin renderer lifetime:
re-read it and bind backend/title/session invalidation at the actual application/ACK boundary.

`reader_snapshot` copies the three 48-byte live reader inputs together with the session token and store generation
under one lock. The real LOD hook consumes a coherent model-detail/object-distance pair; the shadow hook consumes
the same snapshot format. Existing parsing, clamps and local custom-value truncation are retained. The unchanged
fast generation query avoids taking the snapshot lock for every unchanged object. An overlapping update causes
another refresh at the next hook.

These are configuration inputs. `reader_current` only checks that a snapshot has not become stale; it can also be
true for an inactive snapshot. It is not an application receipt. A later adapter must require the active token,
verify actual effects from all required consumers, recheck their generations, and invalidate agreement on changes.
LOD and shadow hooks may observe different generations until they refresh. The shadow base cache is now scoped to
its server address: a different server must not inherit another object's original range merely because values
coincide. Same-address reuse and the object-original caches retain lifetime/thread assumptions. A missing/invalid ShadowServer and unseen/reused LOD
objects need explicit title evidence before claiming complete application. Do not construct an ACK just because
a snapshot was read or a setter returned.

LOD restoration also reconciles game-written fields against the prior written values before every generation
sweep and select, preserving unchanged scaled components separately. Once every LOD override is restored to normal,
its ledger is discarded so a later preference change cannot read retired objects from that old stage. These checks
fix distinct engine rewrites, partial rewrites and unrelated generation bumps. Same-address originals numerically
equal to old scaled fields, and a fresh Max cull flag equal to the old disabled flag, remain ambiguous without real
construction/lifetime evidence. Those native counterexamples stay open; this patch is not a complete application ACK.
Experimental vegetation retains its separate legacy bookkeeping and is outside this conservative profile proof.

Local developer checks:

```sh
bash installer/tools/settings_test.sh
python3 installer/tools/session_reader_effects_test.py --mutations
python3 installer/tools/pipe_state_test.py --runtime "$RUNTIME_BEFORE_7904"
python3 installer/tools/session_startup_test.py
```

The first checks the real store, menu/file behavior and session transitions, including contention. The second
extracts the authored LOD/shadow hooks and links the actual settings store against synthetic VM adapters. It checks
selected object/shadow effects, conservative reset, replacement-server identity, masked local edits, rejected startup
mismatches and restoration. Both bypass-session and old-shadow-cache controls must fail. These tests use no
generated game code or original game inputs. They do not
prove Windows behavior, guest object lifetime, all-object application, network synchronization or a safe start gate.

Local validation for this draft: GCC16.2 passes6,291 maintained assertions; the extracted production hooks pass
32 checks in each of the ordinary and Max-distance fixtures. Bypassing session lookup fails both fixtures;
restoring the old shadow cache fails the replacement-server case; omitting cull-flag restoration fails Max→normal.
Separate independent checks cover2,804 store cases,72 hook cases and168 no-session observations against baseline.
An introduced zero-size query difference was repaired and19 focused query/invalid-buffer controls pass. The final
store's baseline observations also match. The later restoration fixture adds before/after-stage, all-tracked-entry,
partial rewrite and retired-object controls; source-specific results are recorded with the local implementation
checkpoint. Independent b456 baseline fails all seven numeric probes while the corrected code passes67 checks;
the independent retired-object control passes16 checks and rejects baseline/retained-ledger stale access. The first
numeric-only retained-ledger mutation expectation was wrong after fieldwise reconciliation and was replaced.

PIPE witness validation passes3,367 actual extracted body/helper checks and10 rejected wiring mutants; independent
actual producer/getter/startup tests pass63 cases. The first late-publication mutant escaped the original oracle;
additional nested observation points fixed that test gap. The standalone real startup bridge passes54 checks across
four platform/capability configurations; a constant-state mutation fails11. These are native synthetic Windows/D3D
adapters, not actual Windows API or worker-health proof. Full producer and current game/store/startup sources pass
MinGW Windows-target syntax with the actual v0.19 headers. The initial strict-C17 harness rejected an existing
`typeof` extension; the corrected check uses the actual player build's GNU17/GNU++20 dialects. No updated executable
link or title run is established by these checks.
