# Shared Doom target

One JAR for Nokia 6300, 6303 classic and C3-00. Build from the repository root
with `bash s40/build.sh shared`; install `s40/shared/dist/Doom-0.6.2.jar` and
its matching JAD. Requires MIDP 2.0 / CLDC 1.1.

## Play

Put an IWAD at `D:\doom\doom1.wad`, then choose **Check firmware** followed by
**Start Doom**. Fallback folders are `C:\predefgallery\doom\`, `C:\doom\` and
`E:\doom\`. Accepted names also include `doom.wad`, `doom2.wad`, `freedoom1.wad`
and `freedoom2.wad`. Configuration, saves and `doom.log` use the selected IWAD's
directory. Sound is disabled.

RAM Booster is not required. Reboot after using its arena-growth mode before
launching Doom: the retained Java arena reduces available native memory.
Doom renders at 320×200 and presents at 240×180 on the 6300/6303 or 320×240 on
C3, preserving a 4:3 image.

## Controls

| Action | C3-00 (QWERTY or QWERTZ) | 6300 / 6303 classic |
|---|---|---|
| Move / turn | D-pad | D-pad or 2/8/4/6 |
| Fire | Space or center | 5 or center |
| Use / open door | Enter | 0 |
| Strafe left / right | I / O | 1 / 3 |
| Run while held | K or Shift | 7 |
| Map | L | 9 |
| Next weapon | P | # |
| Open / close menu | Right softkey; Backspace opens | Right softkey or * |
| Menu select | Center, Enter, Space or left softkey | Center, 5 or left softkey |
| Back within menu | Backspace | Right softkey closes menu |

C3 also accepts WASD, Q/R for strafe, E for use, C for run, M for map,
N/B for next/previous weapon, and digits 1–8 to select a weapon. Save names
accept ASCII characters, Enter and Backspace. Fn releases printable held keys;
D-pad holds remain independent.

## Diagnostics

Launcher codes: `10` discovery/bootstrap, `20` loader ready, `21` image
allocation, `22` loading, `23` native entry. If launch fails, record the last
code and collect `doom.log` from the IWAD directory. The log includes the build
version, CPU MIDR and startup/frame milestones.

## Development

The engine is in `doomgeneric/`; this directory contains the launcher, input
mapper and native adapter. Shared profiles, discovery and runtime services
come from the S40 SDK. Set `S40_SDK_ROOT` if the SDK is not in sibling `main/`,
or `DOOMGENERIC_DIR` to use another compatible engine checkout.

The engine and `native/doom_s40.c` are GPL-2.0-or-later. The independently
written launcher, input mapper and build configuration are MIT. License
notices are included in the JAR and the `licenses/` directory.
