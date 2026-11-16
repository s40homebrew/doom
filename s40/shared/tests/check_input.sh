#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Run the existing host input regression with memory/undefined-behavior checks.
set -euo pipefail
HERE=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
APP=${S40_APP_DIR:-$(CDPATH='' cd -- "$HERE/.." && pwd)}
SDK=${S40_SDK_ROOT:-$(CDPATH='' cd -- "$HERE/../../../.." && pwd)/main}
BUILD=${S40_BUILD_DIR:-$APP/build}
ENGINE=${DOOMGENERIC_DIR:-$HERE/../../../doomgeneric}
mkdir -p "$BUILD/host-check"
"${HOST_CC:-clang}" -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined -I"$BUILD" -I"$SDK/homebrew/runtime/include" -I"$APP/native" -I"$ENGINE" "$HERE/test_input.c" "$APP/native/input.c" -o "$BUILD/host-check/input-test"
"$BUILD/host-check/input-test"
