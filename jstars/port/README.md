# jstars/port

The game-specific project for the J-Stars Victory VS+ port. It builds `JStarsRecomp.exe` on top of the
ps3recomp runtime. It started from the ps3recomp project template (see `NOTICE.md`).

## What is here

| File | Purpose |
|------|---------|
| `main.cpp` | The runner. Boots the runtime, loads the ELF named on the command line, registers the lifted functions and the HLE handlers, starts the frame clock and runs the entry point. |
| `stubs.cpp` | Game-specific hooks and overrides for functions the runtime does not handle for this title. |
| `jstars_fiber.cpp` | A cellFiber scheduler. The game's job system runs its CPU jobs as fibers. |
| `jstars_game.cpp` | The cellGame data-install handshake that the game does on first boot. |
| `jstars_sysutil.cpp` | cellSearch and cellMusic (system BGM) overrides. |
| `jstars_debug.cpp` | The `PS3_DEBUG` debug console. |
| `CMakeLists.txt` | Links the ps3recomp runtime, the generated sources and the SPU registration. |
| `config.toml` | Lifter and porting settings. |
| `build.bat`, `build_pdb.bat` | Windows builds with clang-cl and Ninja. `build_pdb.bat` builds into `build-pdb` with debug info. |

`recompiled/`, `spu/` and `build/` are generated on your machine and are not stored in this repository.
The project does not link until the lifter has written `recompiled/`, so a fresh checkout fails at the
link step. That is expected.

## Build

The steps are in [../../docs/notes/SETUP.md](../../docs/notes/SETUP.md): load, lift, then `build.bat`.

The EBOOT is taken from your own copy of the game; for how to obtain the decrypted file, see RPCS3 and its documentation.

## Run

The executable takes the path of the decrypted ELF as its first argument:

```
JStarsRecomp.exe path\to\EBOOT.elf
```

The environment variables and the test scripts are described in
[../../docs/notes/SETUP.md](../../docs/notes/SETUP.md).
