# Doom for Nokia 301 Dual SIM

Targets **RM-839 firmware 02.33 only**. Sound is disabled.

## Install and play

1. Install `dist/RM839NativeDoom.jar` and its matching JAD.
2. Copy the unpacked `doom1.wad` to `D:\doom\doom1.wad` on the memory card,
   or `C:\predefgallery\doom\doom1.wad` in phone memory.
3. Open Doom and press **5 / center** or choose **Start Doom**.

Game data is not included. The native search order is `D:\doom\`,
`C:\predefgallery\doom\`, `C:\doom\`, then `E:\doom\`. Accepted IWAD names
are `doom1.wad`, `doom.wad`, `doom2.wad`, `freedoom1.wad` and `freedoom2.wad`.
`doom.log` is written beside the selected IWAD; saves use its `.savegame` tree.
Configuration persistence is disabled by upstream's `ORIGCODE`. Save rename
uses copy/close/unlink and is not atomic.

## Controls

| Keys | Action |
|---|---|
| D-pad / 2, 4, 6, 8 | Move / turn |
| Center / 5 | Fire / menu select |
| 0 | Use / open doors |
| 1 / 3 | Strafe left / right |
| 7 | Hold to run |
| 9 | Automap |
| # | Next owned weapon |
| * / right softkey | Menu / back |
| Left softkey | Select / confirm |

## Build and development

From the repository root:

```sh
bash s40/build.sh rm839
```

Follow the SDK's [dependency setup](https://github.com/s40homebrew/s40/blob/main/docs/building.md).
Requires Clang/LLD, Python, Java 8, the legacy CLDC compiler, S40 API classes,
ProGuard and Arm GNU newlib/libm/libgcc. The output uses ARMv6 soft-float.
Set `S40_SDK_ROOT` if the SDK is not in sibling `main/`.

The engine is in `doomgeneric/`; this directory contains the bootstrap,
native backend and build files. Edit `src/DoomLauncher.java.in` to change
the launcher. Java streams the native image, then the entire game runs in
`JAVA_TASK` with native display, input, timing and filesystem calls.
Native timing polls the clock while pumping messages.

See [firmware.md](docs/firmware.md) for binding and lifetime contracts.
`docs/abi.json` supplies function addresses and ROM fingerprints to the build;
the launcher checks those fingerprints before native entry.
For launch failures, collect `doom.log` and the launcher's check/stage/frame text.
