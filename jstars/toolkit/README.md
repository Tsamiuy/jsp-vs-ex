# Toolkit changes

`ps3recomp-local.patch` holds every change the port makes to the ps3recomp toolkit. It is one patch against ps3recomp
`v0.15.0` (commit `0fe7dbf5db9a467aca268865fb26170ea113c541`). This repository does not contain any ps3recomp code
besides this patch.

- 45 files, about 11,559 lines added and 1,133 removed. It covers the Direct3D 12 renderer (including FXAA and SMAA edge
  smoothing and full-size effect layers), the RSX decompilers,
  the vertex fetch, SPU and SPURS fixes, audio and video codec runtime, pad input, and timer code.
- Made with Git's CRLF conversion switched on, so 492 lines carry CRLF and the rest LF. The `.gitattributes` rule
  `*.patch -text` keeps the bytes as they are. Do not regenerate it with CRLF conversion off, or it fills with false changes.
- Comments inside the patch still name the old `docs/archive/jstars-notes/` folder for some notes; they now live in `docs/notes/`.

`third_party_smaa/` holds four files from SMAA 1x (MIT, see `NOTICE.md`) that the patched video library includes. They
are kept byte for byte (`.gitattributes` has `-text` for the folder), and `apply_toolkit.py` copies them to
`libs/video/third_party_smaa/` in the patched folder. About 1.4 MB, of which `AreaTex.h` is 1.17 MB.

The toolkit itself is the submodule `third_party/ps3recomp` at that commit. `jstars/toolkit/apply_toolkit.py` builds the
patched folder from it (`docs/BUILDING.md`, "Toolkit"); `--check` only checks that the patch applies.

By hand, against a separate clone (nothing is built):

```
git clone https://github.com/sp00nznet/ps3recomp.git
cd ps3recomp
git checkout v0.15.0
git apply --check --whitespace=nowarn /path/to/toolkit/ps3recomp-local.patch
```

The earlier patch files under `docs/archive/jstars-notes/patches/` are the record of single experiments and do not apply in order.
