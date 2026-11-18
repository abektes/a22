#!/bin/sh
# h1's crash hunt (tests/fuzz.c): the core under the compiler's address and undefined-behaviour
# checkers, fed any saved state and any panel input. Usage: sh tests/run_fuzz.sh [SEEDS [FIRST]]
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build
$CC -std=c99 -O1 -g -fno-omit-frame-pointer -ffp-contract=off \
    -fsanitize=address,undefined,float-cast-overflow,float-divide-by-zero -fno-sanitize-recover=all \
    -DH1_FUZZ=1 -Wall -Wextra -Wno-unused-parameter \
    core/*.c tests/fuzz.c -o build/h1fuzz -lpthread
UBSAN_OPTIONS=print_stacktrace=1 ASAN_OPTIONS=detect_leaks=0 build/h1fuzz "${1:-8}" "${2:-1}"
