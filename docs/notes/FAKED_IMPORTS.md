# Faked and missing imports

Sources: `jstars\out\hle_coverage.json` (321 imports: 214 registered, 107 not, **a snapshot from
before the port's own handlers**), STATUS.md, FINDINGS §3, §5, §7, §10, §14. "Port handler"
means implemented in `jstars\port\*.cpp`. For the unlisted ones I did not read the port source;
the JSON says "absent" but a port handler registered by NID may exist, so check
`[hle] unresolved NID` lines in `run\stderr.txt` for the real list of a given run.

## Faked as CELL_OK at boot (confirmed by STATUS.md)
| NID | Name | Could matter later? |
|---|---|---|
| 0x30AA96C4 | cellSpursInitializeWithAttribute2 | Low. SPURS works because the HLE keeps no instance state for it. Revisit if SPURS system workloads are needed. |
| 0x9DCBCB5D | cellSpursAttributeEnableSystemWorkload | Low, same reason. |
| 0x45FE2FCE / 0xDD3B27AC | _sys_spu_printf_initialize / _finalize | Low. Only SPU printf. |
| 0x6CFD856F | cellSysutilGetBgmPlaybackStatus2 | Low. Might matter if the game gates its own BGM on it. |
| 0xE7FA820B | cellSysutil (unknown name) | Unknown. Identify it (DEBUGGING.md, hash candidates) if odd menu behaviour appears. |

## Solved by a port handler (no longer an issue)
| Group | Handled in | FINDINGS |
|---|---|---|
| cellFiber scheduler (Init/Run/WaitSignal/SendSignal/Join/Finalize) | `jstars_fiber.cpp` | §5, §6 |
| cellSpursJq Port2 push/sync/shutdown/join/pool create | `stubs.cpp` | §3 |
| cellVdecOpenEx / QueryAttrEx | `stubs.cpp` | §10 |
| cellSearch / cellMusic (Utility) | `jstars_sysutil.cpp` | §14 |
| UCS2toUTF8 (0xA9A76FB8) | `stubs.cpp` | §7 |
| cellGcmSetDefaultCommandBuffer, cellNetCtlGetState | `stubs.cpp` | §11, RUNTIME_BUGS |

## Not handled and not yet needed (per the coverage JSON)
Ranked by how likely they are to matter once gameplay works:

| Group | Examples | Why it may matter |
|---|---|---|
| **cellAudioAddData** (0xDAB029AA) | | If the game really uses it for sound, audio is silent without it. Medium. |
| **cellSaveData extras** | ListAutoSave, FixedDelete, ListDelete | Saving/deleting saves from menus. Medium once the game reaches the save UI. |
| **cellGamePatchCheck** (0xCE4374F6), cellFsFsync, cellFsSdataOpen | | Patch check and flush; probably harmless offline. Low-medium. |
| **cellL10n** SJIStoUTF8 / UTF8toSJIS (+ 3 unnamed NIDs) | | Japanese text conversion (the game is Japanese-origin). Medium if text is garbled. |
| **cellOskDialog** SetKeyLayoutOption, **cellOskExtUtility** | | On-screen keyboard (names, chat). Low. |
| **sys_net** (socket, bind, sendto, recvfrom, ...) | | Only for online modes. Low offline. |
| **sceNp** (Basic messages, Manager region/network time, Score ranking, Signaling), **sceNp2**, **sceNpTus** | | Online features, rankings. Low offline; may stall a menu waiting for a callback. |
| **cellSysutilAvc2** (voice chat) | | Online voice only. Low. |
| **_cellGcmFunc15** (0x3A33C1FD) | | Graphics-adjacent; could matter for rendering/timing. Check if the black-screen work stalls. |
| **sysPrxForUser** misc (_sys_process_atexitspawn, sys_prx_register_library, ...) | | Many are "name-in-source" (implemented elsewhere); low. |

`name-in-source` / `nid-in-source` in the JSON mean the runtime source has the function but the
NID is not wired; most of those (sys_net, sysPrxForUser, sys_ppu_thread_*) work at run time.

## How to refresh this list
`grep "unresolved NID" jstars\run\stderr.txt | sort | uniq -c`, then name NIDs via
`jstars\out\hle_coverage.json` (DEBUGGING.md).
