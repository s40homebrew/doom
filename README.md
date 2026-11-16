# Doom on Nokia Series 40

DoomGeneric running as native ARM code from a Java ME launcher. Sound is disabled.

## Build

Clone the [S40 homebrew SDK](https://github.com/s40homebrew/s40) into `main/`
beside this fork's `doom/` directory:

```sh
git clone https://github.com/s40homebrew/s40.git main
git clone --branch s40 https://github.com/s40homebrew/doom.git doom
cd doom
```

Follow the SDK's [dependency setup](https://github.com/s40homebrew/s40/blob/main/docs/building.md)
from `main/`, then run the build from `doom/`:

```sh
bash s40/build.sh shared
```

The build finds the sibling `main/` checkout automatically. Set `S40_SDK_ROOT`
to the SDK repository's absolute path if it is checked out elsewhere.

| Target | Phones | Output directory |
| --- | --- | --- |
| `shared` (default) | 6300 / 6303 classic / C3-00 | `s40/shared/dist/` |
| `rm839` | Nokia 301, RM-839 firmware 02.33 | `s40/rm839/dist/` |
| `rm443` | 6303 classic, firmware 10.12 | `s40/rm443/dist/` |

Replace `shared` with the target you want. Each produces a `.jar` and matching
`.jad`. The shared target discovers firmware services; the other two use fixed addresses.
The shared package is `Doom-0.6.1.jar`. See the
[shared target guide](s40/shared/README.md) for controls and setup.

See the [RM-443](s40/rm443/README.md) and [RM-839](s40/rm839/README.md)
guides for fixed-target setup and ABI details.

## Play

Put an IWAD at `D:\doom\doom1.wad`, install the JAR/JAD and choose **Start Doom**.
The shared target has a **Check firmware** step first. Other accepted names are
`doom.wad`, `doom2.wad`, `freedoom1.wad` and `freedoom2.wad`. Fallback folders:
`C:\predefgallery\doom\`, `C:\doom\` and `E:\doom\`.
Saves and `doom.log` go beside the IWAD.

D-pad or 2/4/6/8 moves; 5 fires; 0 uses doors; 1/3 strafes; 7 runs; 9 opens the
map; # changes weapons; * or the right softkey opens the menu.
[C3 keyboard controls](s40/shared/README.md).

## Source and licenses

Based on [DoomGeneric](https://github.com/ozkl/doomgeneric); engine source is in
`doomgeneric/`, S40 launchers and adapters in `s40/`.
Doom and its derived backends are [GPL-2.0-or-later](LICENSE). The independent
shared launcher, input mapper and build configuration are [MIT](s40/LICENSE).
Library notices are in the SDK and `s40/rm839/licenses/`.
