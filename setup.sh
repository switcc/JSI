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

# Pick a download tool
download() {
    local url="$1" dest="$2"
    if command -v curl &>/dev/null; then
        curl -fSL --retry 3 -o "${dest}" "${url}"
    elif command -v wget &>/dev/null; then
        wget -q -O "${dest}" "${url}"
    else
        echo "ERROR: Neither curl nor wget found. Install one and retry."
        return 1
    fi
}

# ── QuickJS ────────────────────────────────────────────────────────────────
fetch_quickjs() {
    local dest="${THIRD_PARTY}/quickjs"
    if [[ -f "${dest}/quickjs.h" ]]; then
        echo "QuickJS already present at ${dest}"
        return 0
    fi

    echo "Fetching QuickJS..."
    rm -rf "${dest}"
    mkdir -p "${dest}"

    local tarball="${THIRD_PARTY}/quickjs.tar.gz"
    local ok=0

    # Strategy 1: GitHub tarball from bellard/quickjs (no auth required)
    if [[ $ok -eq 0 ]]; then
        echo "  Trying GitHub archive download (bellard/quickjs)..."
        download "https://github.com/bellard/quickjs/archive/refs/heads/master.tar.gz" \
                 "${tarball}" 2>/dev/null && ok=1 || true
    fi

    # Strategy 2: Bellard's website release
    if [[ $ok -eq 0 ]]; then
        echo "  Trying bellard.org release..."
        download "https://bellard.org/quickjs/quickjs-2024-01-13.tar.xz" \
                 "${THIRD_PARTY}/quickjs.tar.xz" 2>/dev/null \
            && tar xf "${THIRD_PARTY}/quickjs.tar.xz" -C "${THIRD_PARTY}" \
            && mv "${THIRD_PARTY}"/quickjs-2024-01-13/* "${dest}/" \
            && rm -rf "${THIRD_PARTY}/quickjs-2024-01-13" "${THIRD_PARTY}/quickjs.tar.xz" \
            && ok=2 || true
    fi

    # Strategy 3: git clone over SSH as last resort
    if [[ $ok -eq 0 ]]; then
        echo "  Trying git clone over SSH..."
        git clone --depth 1 git@github.com:bellard/quickjs.git "${dest}" 2>/dev/null && ok=3 || true
    fi

    if [[ $ok -eq 0 ]]; then
        rm -rf "${dest}" "${tarball}"
        echo ""
        echo "ERROR: Could not download QuickJS. Please manually place the source in:"
        echo "  ${dest}/"
        echo ""
        echo "You can download it from:"
        echo "  https://github.com/bellard/quickjs"
        echo "  https://bellard.org/quickjs/"
        return 1
    fi

    # If we downloaded a GitHub tarball, extract it
    if [[ $ok -eq 1 ]]; then
        tar xzf "${tarball}" -C "${dest}" --strip-components=1
        rm -f "${tarball}"
    fi

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
