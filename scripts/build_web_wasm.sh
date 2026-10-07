#!/usr/bin/env bash
# Builds the core, its unit tests and the probe for wasm32 with Emscripten and
# runs both under Node. The shape follows rePIU's scripts/build_web_wasm.sh:
# the toolchain is checked and named up front, because without that the
# failure arrives as hundreds of header errors with the cause buried.
set -euo pipefail

configuration="Release"
if [[ "${1:-}" == "--config" ]]; then
    configuration="${2:?--config needs a value}"
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="$root/build/web-wasm-$(echo "$configuration" | tr '[:upper:]' '[:lower:]')"

if ! command -v emcmake > /dev/null 2>&1; then
    emsdk_root="${EMSDK:-$HOME/emsdk}"
    if [[ -f "$emsdk_root/emsdk_env.sh" ]]; then
        cat >&2 <<NEEDS
emsdk is installed at $emsdk_root but not active in this shell:

    source "$emsdk_root/emsdk_env.sh"

NEEDS
    else
        cat >&2 <<'NEEDS'
Emscripten is not available. To install it:

    git clone --depth 1 https://github.com/emscripten-core/emsdk.git ~/emsdk
    cd ~/emsdk && ./emsdk install latest && ./emsdk activate latest
    source ~/emsdk/emsdk_env.sh

NEEDS
    fi
    exit 1
fi

emcmake cmake -S "$root" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE="$configuration" -DREX86_BUILD_TESTS=ON
cmake --build "$build_dir" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-$(nproc 2>/dev/null || echo 2)}"
ctest --test-dir "$build_dir" --output-on-failure

echo
echo "Output directory: $build_dir"
echo "Run the probe with:  node $build_dir/bin/rex86_probe.js"
