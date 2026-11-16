# Native Doom for Nokia 6303 classic

Targets **RM-443 firmware 10.12 only**. Sound and multiplayer are disabled.

## Install and play

1. Put `doom1.wad` at `D:\doom\doom1.wad` on the memory card, or at
   `C:\predefgallery\doom\doom1.wad` in phone memory.
2. Install `dist/RM443NativeDoom.jar` and its matching JAD, then choose **Start Doom**.
3. Press `*` for the menu; use directions and `5` or the left softkey to select.

Game data is not included. The [original shareware package](https://www.gamers.org/pub/idgames/idstuff/doom/doom19s.zip)
contains `doom1.wad`. The adapter also accepts `doom.wad`, `doom2.wad`,
`freedoom1.wad` and `freedoom2.wad`.

Search order: `D:\doom\`, `C:\predefgallery\doom\`, `C:\doom\`, `E:\doom\`.
`doom.log` is written beside the selected IWAD; saves use its `.savegame`
subdirectory. An empty save slot supplies the name `S40 SAVE n`; press Select
to confirm. Save rename uses copy/close/unlink and is not atomic. Upstream's
ordinary configuration persistence remains disabled by `ORIGCODE`.

## Controls

| Key | Action |
|---|---|
| D-pad or 2 / 8 / 4 / 6 | Forward / back / turn left / turn right |
| Center / 5 | Fire and menu select |
| Left softkey | Menu select / confirm |
| Right softkey or * | Menu / back / cancel |
| 0 | Open doors / use |
| 1 / 3 | Strafe left / right |
| 7 | Hold to run |
| 9 | Automap |
| # | Next owned weapon |

Doom renders at 320×200 and presents at 240×180, centered on the 240×320 LCD.
Quit and native UI focus loss return to the launcher and release game resources.

## Diagnostics

Collect `doom.log` and the launcher's check/status, stage, frame count and error
text after a failure. Stage codes are locations, not a progress percentage:
1 native entry, 2 event pump, 3 LCD initialization, 4 IWAD selected,
5 engine initialization returned, 6 frame rendered.

Check 18 indicates loader failure; status 22 is allocation failure. Loader
errors 20–26 cover descriptor, allocation, chunk and relocation failures.
`Exit 1000` in the log is the normal longjmp shutdown sentinel; successful
cleanup returns status 0 to the launcher.

## Build and development

From the repository root, run `bash s40/build.sh rm443`.
Follow the SDK's [dependency setup](https://github.com/s40homebrew/s40/blob/main/docs/building.md).
The build uses Clang/LLD targeting ARMv5TE soft-float, Java 8, the legacy CLDC
compiler, S40 API classes and ProGuard. Set `S40_SDK_ROOT` if the SDK is not
in sibling `main/`; tool paths can be overridden through `LLVM`, `CC`, `LD`,
`JAVA8_HOME`, `LEGACY_JAVAC`, `SDK_CLASSES` and `PROGUARD_JAR`.

The engine is in `doomgeneric/`; its S40 adaptations include tick conversion,
keypad save names and the native stdio adapter. The loader streams the ARM image
into OS memory, uses a private 192 KiB stack and runs the engine in `JAVA_TASK`.
The game requests a 6 MiB zone, falling back to 2 MiB. Each launch loads a fresh
native image. The native loop handles display, input, timing and files without
per-frame Java calls.

Read the [firmware ABI notes](docs/firmware.md) before changing bindings or the
native message pump. Generated imports, link map and loader disassembly are
in `build/imports.json`, `build/doom.map` and `build/loader.disasm.txt`.
