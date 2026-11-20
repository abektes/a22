#!/bin/sh
# Builds dist/a22.rfm1: the core as a WebAssembly module behind the fm1core ABI (port/rfm1), the file the
# FM-1 browser simulator (github.com/jakehandy/fm1-browser-simulator) and other .rfm1 hosts load.
# Needs clang with the wasm32 target and wasm-ld (auto-detects Homebrew LLVM / Emscripten LLVM on macOS).
#   tools/build_rfm1.sh [VERSION]
set -e
cd "$(dirname "$0")/.."
mkdir -p build dist

CLANG="${WASM_CLANG:-}"
if [ -z "$CLANG" ]; then
    for d in /opt/homebrew/Cellar/emscripten/*/libexec/llvm/bin /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin; do
        if [ -x "$d/clang" ] && [ -x "$d/wasm-ld" ]; then
            CLANG="$d/clang"
            PATH="$d:$PATH"
            export PATH
            break
        fi
    done
fi
CLANG="${CLANG:-clang}"

"$CLANG" --target=wasm32 -std=c99 -O2 -ffreestanding -fno-builtin -nostdlib -mnontrapping-fptoint -mbulk-memory \
    -msign-ext -mmutable-globals -mno-reference-types -mno-multivalue \
    -Wall -Wextra -Wdouble-promotion -Wshadow -Wno-unused-parameter -fno-fast-math -ffp-contract=off \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=131072 \
    -Wl,--initial-memory=4194304 -Wl,--max-memory=4194304 \
    -DH1_RELEASE="\"${1:-dev}\"" \
    core/*.c port/rfm1/rfm1.c -o build/a22-core.wasm
python3 tools/pack_rfm1.py --version "${1:-dev}"
