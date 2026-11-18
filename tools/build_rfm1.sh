#!/bin/sh
# Builds dist/h1.rfm1: the core as a WebAssembly module behind the fm1core ABI (port/rfm1), the file the
# FM-1 browser simulator (github.com/jakehandy/fm1-browser-simulator) and other .rfm1 hosts load.
# Needs clang 18 or newer with the wasm32 target and wasm-ld. No emscripten, no libc.
#   tools/build_rfm1.sh [VERSION]
set -e
cd "$(dirname "$0")/.."
mkdir -p build dist
clang --target=wasm32 -std=c99 -O2 -ffreestanding -fno-builtin -nostdlib -mnontrapping-fptoint -mbulk-memory \
    -msign-ext -mmutable-globals -mno-reference-types -mno-multivalue \
    -Wall -Wextra -Wdouble-promotion -Wshadow -Wno-unused-parameter -fno-fast-math -ffp-contract=off \
    -Wl,--no-entry -Wl,--strip-all -Wl,-z,stack-size=131072 \
    -Wl,--initial-memory=4194304 -Wl,--max-memory=4194304 \
    -DH1_RELEASE="\"${1:-dev}\"" \
    core/*.c port/rfm1/rfm1.c -o build/h1-core.wasm
python3 tools/pack_rfm1.py --version "${1:-dev}"
