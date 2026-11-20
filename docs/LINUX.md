# Native Linux (experimental)

**Play on Linux through Proton.** The supported way to play on Linux and on the Steam Deck is the Windows build run under
Proton, as described in [INSTALL.md](INSTALL.md). Everything on this page is early, experimental work towards a native
Linux build with a Vulkan renderer. It does not build or run the game yet, and nothing here changes the Windows build.

## Where it stands

| Step | State |
|---|---|
| The patched toolkit (ps3recomp v0.19.0 plus `installer/port/patches`) builds natively on Linux x86_64 with its Vulkan renderer | done: clang and GCC, no errors |
| The toolkit's own renderer test scenes run on Mesa's CPU Vulkan driver (lavapipe), under the Khronos validation layer | done: all scenes pass, no validation messages |
| The toolkit's boot smoke test (a small synthetic program, no game) runs natively | done: passes (it draws through the toolkit's null renderer) |
| The renderer's own built-in shaders (the depth copy, edge smoothing, SMAA, overlay and tint passes and the vertex-pulling shader) translate to SPIR-V | done: all 25 translate and validate; none has been drawn with on Vulkan |
| The port (`installer/port/jsp`) compiles and links natively | not yet |
| The game's own shaders translate to SPIR-V | not tried: that needs shaders from a game run |
| The game boots natively and draws a frame | not yet |
| J-Stars' renderer work (resolution scale, edge smoothing, shadow fixes, the in-game panels and overlays) on Vulkan | not yet: it is written for Direct3D 12 only |
| Rollback between a native Linux player and a Windows player | not yet |

## Packages

A C/C++ compiler (clang: the port's SPU dispatcher needs it; GCC also builds the toolkit), CMake 3.20 or newer, Ninja, Python 3,
Git, and the development packages of SDL2, the Vulkan headers and loader, glslang (with its CMake files) and SPIRV-Tools. For the
test scenes without a GPU: Mesa's lavapipe driver and the Khronos validation layer.

- Debian and Ubuntu (the list upstream ps3recomp's own Linux CI uses):
  `sudo apt install git python3 cmake ninja-build clang libsdl2-dev glslang-dev glslang-tools spirv-tools libvulkan-dev mesa-vulkan-drivers vulkan-validationlayers`
  (Ubuntu's glslang CMake files need `glslang-tools` too.)
- Fedora: `sudo dnf install git python3 cmake ninja-build clang SDL2-devel glslang-devel glslang spirv-tools vulkan-headers vulkan-loader-devel mesa-vulkan-drivers vulkan-validation-layers`
- Arch: `sudo pacman -S git python cmake ninja clang sdl2 glslang spirv-tools vulkan-headers vulkan-icd-loader vulkan-swrast vulkan-validation-layers`

The Fedora and Arch names have not been tried. The steps below were run on NixOS with the same packages from its own
collection: clang 19.1.7, GCC 16.2.0, CMake 4.4.4, SDL2 2.32 (sdl2-compat), glslang 16.6.0, Vulkan headers, loader and
validation layer 1.4.363, Mesa 26.2.5.

## Build the toolkit and run its test scenes

From a clone with the submodule (`git submodule update --init`):

```sh
python3 installer/tools/export_toolkit.py --out build/linux/ps3recomp
CC=clang CXX=clang++ cmake -S build/linux/ps3recomp -B build/linux/tk -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS3RECOMP_RSX_VULKAN=ON
cmake --build build/linux/tk
```

Check the configure output for `Vulkan:       guest programs available (glslang ...)`. Without it glslang was not found, the
shader translator is a stub that always fails, and the scenes below with guest programs cannot pass.

The scenes are the ones upstream's `linux-vulkan.yml` runs. Point the Vulkan loader at lavapipe (the file name varies by
distribution, for example `/usr/share/vulkan/icd.d/lvp_icd.x86_64.json` or `lvp_icd.json`):

```sh
export VK_DRIVER_FILES=/usr/share/vulkan/icd.d/lvp_icd.x86_64.json
cd build/linux/tk
./test_shader_spirv
# fixed-function path
for m in "" --draw "--draw --frames=60" --depth --tex --tex-io --quads --threads --split-present; do ./ps3recomp_host $m || echo "FAIL $m"; done
# the shared draw engine with guest programs, under the validation layer
for m in "" --draw --depth --quads --shader --tex --tex-io --mip --rtt --depthtex --mrt --mrt-a --cube --depthbounds \
         --split-present --feedback --clip --clip-keep --float-rt --float-rt-r; do
  PS3RECOMP_VK_GUEST_PROGRAMS=1 VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation ./ps3recomp_host $m > run.log 2>&1 || echo "FAIL $m"
  grep -q VUID run.log && echo "VALIDATION $m"
  grep -q "register-file draw engine path" run.log || echo "NOT ON THE ENGINE $m"
done
# boot smoke test (synthetic program, no game)
./ps3recomp_boot_smoke smoke/smoke.elf | grep -E "^\[smoke\] (PASS|FAIL)"
```

No output other than the scenes' own lines means every scene passed.
