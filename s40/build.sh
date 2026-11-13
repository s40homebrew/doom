#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
P=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
export S40_SDK_ROOT=${S40_SDK_ROOT:-$(CDPATH='' cd -- "$P/../.." && pwd)/main}
[[ -f "$S40_SDK_ROOT/tools/env.sh" ]] || { echo "Set S40_SDK_ROOT to the S40 homebrew source root" >&2; exit 1; }
source "$S40_SDK_ROOT/tools/env.sh"
case "${1:-shared}" in
    shared)
        export S40_APP_DIR="$P/shared"
        export S40_BUILD_DIR="$P/shared/build"
        export S40_DIST_DIR="$P/shared/dist"
        exec bash "$S40_SDK_ROOT/homebrew/build.sh" doom ;;
    rm443|rm839) exec bash "$P/$1/build.sh" ;;
    *) echo "Usage: bash s40/build.sh [shared|rm443|rm839]" >&2; exit 1 ;;
esac
