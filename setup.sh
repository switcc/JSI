#!/usr/bin/env bash
#
# setup.sh – Download and prepare JS engine dependencies for JSI.
#
# Usage:
#   ./setup.sh              # download all engines
#   ./setup.sh quickjs      # download QuickJS only
#   ./setup.sh v8           # download V8 only
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
THIRD_PARTY="${SCRIPT_DIR}/third_party"
mkdir -p "${THIRD_PARTY}"

# ── QuickJS ────────────────────────────────────────────────────────────────
fetch_quickjs() {
    local dest="${THIRD_PARTY}/quickjs"
    if [[ -f "${dest}/quickjs.h" ]]; then
        echo "QuickJS already present at ${dest}"
        return 0
    fi

    echo "Fetching QuickJS..."
    rm -rf "${dest}"

    echo "  Cloning bellard/quickjs..."
    git clone --depth 1 https://github.com/bellard/quickjs.git "${dest}" || {
        rm -rf "${dest}"
        echo ""
        echo "ERROR: Could not clone QuickJS. Please manually place the source in:"
        echo "  ${dest}/"
        echo ""
        echo "You can download it from:"
        echo "  https://github.com/bellard/quickjs"
        return 1
    }

    echo "QuickJS ready at ${dest}"
}

# ── V8 ─────────────────────────────────────────────────────────────────────
fetch_v8() {
    echo "V8 setup instructions:"
    echo ""
    echo "V8 is large and has its own build system (gn + ninja)."
    echo "The recommended approach is to install a pre-built package:"
    echo ""
    echo "  Ubuntu/Debian:"
    echo "    sudo apt-get install libv8-dev"
    echo ""
    echo "  macOS (Homebrew):"
    echo "    brew install v8"
    echo ""
    echo "  From source:"
    echo "    See https://v8.dev/docs/build"
    echo "    After building, pass -DV8_ROOT=/path/to/v8 to cmake."
    echo ""
}

# ── Main ───────────────────────────────────────────────────────────────────
target="${1:-all}"

case "${target}" in
    quickjs) fetch_quickjs ;;
    v8)      fetch_v8 ;;
    all)
        fetch_quickjs
        echo ""
        fetch_v8
        ;;
    *)
        echo "Usage: $0 [quickjs|v8|all]"
        exit 1
        ;;
esac

echo ""
echo "Done. Now build with:"
echo "  cmake -B build && cmake --build build"
