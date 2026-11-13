# SPDX-License-Identifier: MIT
# shellcheck shell=bash disable=SC2034
# Sourced by homebrew/build.sh; the shared runtime never reads Doom's Makefile.
ARTIFACT=RM443AdaptiveDoom
DOOMGENERIC_DIR=${DOOMGENERIC_DIR:-$EXAMPLE_DIR/../../doomgeneric}
APP_FLAGS=("-ffile-prefix-map=$EXAMPLE_DIR/../..=." -I"$DOOMGENERIC_DIR" -I"$EXAMPLE_DIR/native" -DDOOMGENERIC_S40
    -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -DNORMALUNIX)
# Compile the included engine source with its S40 adaptations.
while IFS= read -r filename; do
    APP_SOURCES+=("$DOOMGENERIC_DIR/$filename")
done < <(python3 - "$DOOMGENERIC_DIR/Makefile" <<'PY'
import re
import sys
from pathlib import Path
sources = re.search(r'^SRC_DOOM = (.+)$', Path(sys.argv[1]).read_text(), re.M)[1]
for name in sources.split():
    if name not in ['i_system.o', 'doomgeneric_xlib.o']:
        print(name[:-2] + '.c')
PY
)
APP_SOURCES+=("$EXAMPLE_DIR/native/doom_s40.c" "$EXAMPLE_DIR/native/input.c")
