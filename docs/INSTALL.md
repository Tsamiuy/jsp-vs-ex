# Install and play

This page takes you from nothing to a running game on Windows. The short version is in the [README](../README.md#download-and-play).

Contents: [What you need](#what-you-need) · [Steps](#steps) · [What setup refuses](#what-setup-refuses) · [Saves](#saves) · [Update](#update) · [Uninstall](#uninstall) · [Check your download](#check-your-download)

## What you need

| Need | Detail |
|---|---|
| Windows | Windows 10 or 11, x64. The install is per user and needs no admin rights. |
| Your game | The disc folder (the one with `PS3_GAME`) of the US disc BLUS31519 or the EU disc BLES02136. |
| The decrypted EBOOT | `EBOOT.elf` from your own copy, see RPCS3 and its documentation. Nothing from the game is shipped here. |
| CPU | AVX2 and FMA: Intel Haswell (2013) or newer, AMD Excavator or Zen. The port is built for it by default (`JSP_AVX2`, [installer/port/jsp/CMakeLists.txt](../installer/port/jsp/CMakeLists.txt)). Setup does not test for it. The game has not been tried on an older CPU, and there it will most likely not run. A hand build can use `-DJSP_AVX2=OFF` for any x86-64 CPU. |
| Disk space | Setup refuses to build with less than 4 GB free where the build goes, and warns under 10 GB. The game data install needs about 2.5 GB more, in your user data folder. It runs the first time you start the game. |
| Memory | The build runs one compile per CPU core, and no more than there are free GB of RAM (about 1 GB each). |
| Graphics | A Windows 10 or 11 graphics driver with Direct3D 12. Play testers used AMD and NVIDIA graphics. No minimum GPU is stated or checked in the code. |
| Setup tools | Git, Python, Inno Setup and an internet connection, only to build `jsp-setup.exe` yourself. The ready-made download needs none of them. |

Linux and Steam Deck (Proton) are not supported yet.

## Steps

You can use the ready-made download or build the setup program yourself. Both build the game on your PC from your own copy.

1. Get `jsp-setup.exe`. Either download it, or the portable zip, from the newest release on the [releases page](https://github.com/Tsamiuy/jsp-vs-ex/releases); or build it yourself, see [installer/README.md, "Building the installer"](../installer/README.md#building-the-installer-windows). You now have the file on your disk.
2. Run `jsp-setup.exe`. Windows may warn that it is not code-signed, see [Check your download](#check-your-download). The setup wizard opens.
3. Point the wizard at your disc folder and at `EBOOT.elf`. It checks both and says in plain words if one is wrong, see [What setup refuses](#what-setup-refuses).
4. Let setup finish. It builds the port on your PC. The first build takes a while and needs several GB of free space. When it ends you see the finish page.
5. Open **JSP Fan Revival** from the Start menu. The launcher opens.
6. Start the game from the launcher. The first start installs the game data (about 2.5 GB), then the opening movie plays and the menus open.

If the build stops, click **Build again** in the Start menu. It resumes where it stopped. More in [TROUBLESHOOTING.md](TROUBLESHOOTING.md).

## What setup refuses

Setup checks your inputs before the long build. It says why in plain words, with a code. The full list is in [installer/README.md, "Checks before the build"](../installer/README.md#checks-before-the-build).

| Code | What you see | What to do |
|---|---|---|
| `D1`, `D2`, `D3` | The folder is not the disc folder (no `PS3_GAME`, `PARAM.SFO` or `USRDIR`), or the game is not US BLUS31519 or EU BLES02136. | Pick the disc folder. Other regions, such as the Japanese release, are refused (`D2`). |
| `E1`, `E2` | The file is the encrypted `EBOOT.BIN`, is not a PowerPC64 ELF, or has an implausible size. | Use the decrypted `EBOOT.elf`. |
| `E3` | The `EBOOT.elf` is from the other region than the disc folder, or is a different build. The game update 1.02 and digital editions are not supported. | Use the 1.00 disc `EBOOT.elf` of the same region as the disc folder. |
| `P1`, `P2` | The install or disc path has characters that break the build. | Use a path without them (the list is in installer/README.md). |
| `S1` | A folder is not writable, or there is too little free space. | Free space or pick another drive. |

## Saves

- Saves are in `%APPDATA%\JSP Fan Revival\hdd0` (user `00000001`). A portable install keeps them in `hdd0` in the install folder.
- The Cheats tab in the settings window makes a checked, dated `BACKUP-<folder>-<date>-<time>` copy of the save folder before an unlock ([UNLOCK_ALL.md](notes/UNLOCK_ALL.md)). Copy the folder yourself for any other backup.
- Setup can import an RPCS3 save of the game. It lists what would be copied and replaced first, and never writes to RPCS3. See [SAVE_IMPORT_SAFETY.md](notes/SAVE_IMPORT_SAFETY.md). Close the game and RPCS3 first.

## Update

- Run the newer `jsp-setup.exe` over the old install. It has the same app id. A changed input, such as another EBOOT, lifts again; finished steps are kept otherwise. Installing over an older version has not been re-tested for this text.
- For the portable zip, unpack the newer one into a fresh folder and copy `hdd0` over.

## Uninstall

Open Windows Settings, then Apps, or use the uninstaller in the install folder. It asks whether to also delete saves and the game data. The default, No, keeps them. For a silent uninstall, `/REMOVEDATA=1` deletes them.

## Check your download

`jsp-setup.exe` and the game it builds are not code-signed. Windows SmartScreen or an antivirus program may warn about them, because the program compiles code and writes files in your user folders.

Each release lists SHA-256 sums on its release page, and a `.sha256` file for the zip. The sums file is `SHA256SUMS.txt` for 0.3.0 and earlier, and `SHA256SUMS` from the next release. To check a download, run this in PowerShell and compare the value with the release page:

```
Get-FileHash .\jsp-setup.exe -Algorithm SHA256
```
