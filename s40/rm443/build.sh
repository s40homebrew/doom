#!/usr/bin/env bash
set -euo pipefail
P=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
S40_SDK_ROOT=${S40_SDK_ROOT:-$(CDPATH='' cd -- "$P/../../.." && pwd)/main}
[[ -f "$S40_SDK_ROOT/tools/env.sh" ]] || { echo "Set S40_SDK_ROOT to the S40 homebrew source root" >&2; exit 1; }
source "$S40_SDK_ROOT/tools/env.sh"
R="$S40_SDK_ROOT"
B="$P/build"
UPSTREAM="$P/../../doomgeneric"
for t in "$CC" "$LD" "$LLVM/llvm-objcopy" "$JAVA8_HOME/bin/java"; do
    test -x "$t" || { printf 'Missing tool: %s\n' "$t"; exit 1; }
done
python3 "$R/scripts/clean_build_outputs.py" java "$P" "$B" build "$P"
mkdir -p "$B/obj" "$B/classes" "$B/preverified" "$P/dist"
# Compile the included engine source, including the S40 adaptations.
python3 "$P/scripts/gen_imports.py"
FLAGS=(-target arm-none-eabi -march=armv5te -marm -mfloat-abi=soft -O2 -fPIC -fvisibility=hidden -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-strict-aliasing -fwrapv -fno-common -ffunction-sections -fdata-sections -Wno-incompatible-pointer-types-discards-qualifiers -Wno-pointer-sign -Wno-deprecated-non-prototype -I"$P/native/include" -I"$P/native" -I"$UPSTREAM" -DDOOMGENERIC_S40 -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DNORMALUNIX)
# Z_ChangeTag embeds __FILE__ in runtime errors even without debug symbols.
# Keep useful workspace-relative filenames in every native compilation,
# including future debug builds, without exposing the builder's home path.
FLAGS+=("-ffile-prefix-map=$R=." "-ffile-prefix-map=$P/../..=." "-ffile-prefix-map=$P=.")
# Same portable engine source list as upstream, with the host system backend
# replaced by doom_s40.c. Sound backends are absent (FEATURE_SOUND undefined).
SOURCES=$(python3 - "$UPSTREAM/Makefile" <<'PY'
import re,sys
s=re.search(r'^SRC_DOOM = (.+)$',open(sys.argv[1]).read(),re.M)[1]
print(' '.join(x[:-2]+'.c' for x in s.split() if x not in ['i_system.o','doomgeneric_xlib.o']))
PY
)
printf 'Compiling freestanding ARMv5TE DoomGeneric...\n'
for s in $SOURCES; do
    "$CC" "${FLAGS[@]}" -c "$UPSTREAM/$s" -o "$B/obj/${s%.c}.o"
done
for s in libc doom_s40 div64; do
    "$CC" "${FLAGS[@]}" -c "$P/native/$s.c" -o "$B/obj/$s.o"
done
"$CC" "${FLAGS[@]}" -c "$B/imports.S" -o "$B/obj/imports.o"
"$CC" "${FLAGS[@]}" -c "$P/native/div64.S" -o "$B/obj/div64-abi.o"
"$LD" -shared -Bsymbolic --no-undefined --gc-sections -e native_main --hash-style=sysv -z max-page-size=4096 -Map="$B/doom.map" -o "$B/doom.elf" "$B"/obj/*.o
python3 "$P/scripts/pack.py"
"$LLVM/llvm-readobj" --file-headers --program-headers --relocations "$B/doom.elf" > "$B/doom.elf.txt"
printf 'Building bootstrap loader and CLDC launcher...\n'
"$CC" "${FLAGS[@]}" -Os -c "$P/native/loader.c" -o "$B/loader-c.o"
"$CC" "${FLAGS[@]}" -c "$P/native/loader.S" -o "$B/loader-s.o"
"$LD" -T "$P/native/loader.ld" -o "$B/loader.elf" "$B/loader-s.o" "$B/loader-c.o"
"$LLVM/llvm-objcopy" -O binary "$B/loader.elf" "$B/native.bin"
"$CC" "${FLAGS[@]}" -c "$P/native/stage.S" -o "$B/stage.o"
"$LD" --image-base=0 -Ttext=0 -e stage_start -o "$B/stage.elf" "$B/stage.o"
"$LLVM/llvm-objcopy" -O binary "$B/stage.elf" "$B/stage.bin"
"$LLVM/llvm-objdump" -d "$B/loader.elf" > "$B/loader.disasm.txt"
"$JAVA8_HOME/bin/java" -cp "$LEGACY_JAVAC" j2me.thirdparty.legacyjavac.Main -source 1.3 -target 1.1 -g:none -bootclasspath "$SDK_CLASSES" -d "$B/classes" "$P/src/DoomLauncher.java"
python3 "$R/homebrew/tools/preverify.py" "$JAVA8_HOME/bin/java" "$PROGUARD_JAR" "$SDK_CLASSES" "$B/classes" "$B/preverified"
"$JAVA8_HOME/bin/javap" -classpath "$B/preverified" -c -p -v DoomLauncher > "$B/DoomLauncher.javap.txt"
# Exercise the compiled selector without instantiating a MIDlet or running ARM code.
mkdir -p "$B/host-check"
"$JAVA8_HOME/bin/javac" -d "$B/host-check" "$P/scripts/CheckStagePlacement.java"
"$JAVA8_HOME/bin/java" -cp "$B/host-check:$B/preverified:$SDK_CLASSES" CheckStagePlacement "$B/native.bin"
cp "$B/stage.bin" "$B/native.bin" "$B/doom.bin" "$B/preverified/"
python3 - "$P" <<'PY'
from pathlib import Path
import sys, zipfile
p=Path(sys.argv[1]);d=p/'dist';manifest=(p/'src/MANIFEST.MF').read_text().rstrip()+'\n\n'
# Write the supplied manifest verbatim with JAR-required CRLF endings. The
# JDK jar tool would append Created-By with the local JDK version and vendor.
# Use the ZIP epoch for every entry, including the manifest. ZipFile.write()
# copies filesystem mtimes; writestr(name, ...) uses the current local time.
zip_epoch=(1980,1,1,0,0,0)
with zipfile.ZipFile(d/'RM443NativeDoom.jar','w',zipfile.ZIP_DEFLATED) as archive:
    def write_entry(name,data):
        entry=zipfile.ZipInfo(name,date_time=zip_epoch)
        entry.compress_type=zipfile.ZIP_DEFLATED
        entry.external_attr=0o100644 << 16
        archive.writestr(entry,data)
    write_entry('META-INF/MANIFEST.MF',manifest.replace('\n','\r\n').encode())
    write_entry('licenses/COPYING.DOOM',(p/'COPYING').read_bytes())
    write_entry('licenses/LICENSE.MIT',(p.parent/'LICENSE').read_bytes())
    classes=p/'build/preverified'
    for path in sorted(classes.rglob('*')):
        if path.is_file():
            name=path.relative_to(classes).as_posix()
            if name=='META-INF/MANIFEST.MF':
                raise SystemExit('Unexpected manifest in preverified output')
            write_entry(name,path.read_bytes())
m=manifest.replace('Manifest-Version: 1.0\n','').strip()
# Inspect decompressed entries: scanning the compressed JAR itself misses
# embedded paths. Reject macOS/Linux home paths and this workspace's root.
with zipfile.ZipFile(d/'RM443NativeDoom.jar') as archive:
    for entry in archive.infolist():
        if entry.date_time!=zip_epoch or entry.extra:
            raise SystemExit('Unexpected ZIP timestamp/extra metadata: '+entry.filename)
        data=archive.read(entry)
        if any(prefix in data for prefix in (str(p).encode(), b'/Users/', b'/home/')):
            raise SystemExit('Host path leaked into JAR entry: '+entry.filename)
m+='\nMIDlet-Jar-URL: RM443NativeDoom.jar\nMIDlet-Jar-Size: '+str((d/'RM443NativeDoom.jar').stat().st_size)+'\n'
(d/'RM443NativeDoom.jad').write_bytes(m.replace('\n','\r\n').encode())
print('Built',d/'RM443NativeDoom.jar')
PY
