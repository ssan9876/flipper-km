#!/usr/bin/env bash
# Host-side unit tests for the pure C core (no Flipper dependencies).
set -e
cd "$(dirname "$0")"

# Pick a C compiler. Override with KM_CC if you have a preference.
# Held as an array because "zig cc" is two words and install paths on
# Windows contain spaces.
CC_CMD=()

detect_cc() {
    if [ -n "$KM_CC" ]; then
        CC_CMD=("$KM_CC")
        return
    fi
    if command -v zig >/dev/null 2>&1; then
        CC_CMD=(zig cc)
        return
    fi
    # zig installed via winget but not yet on PATH in this shell
    local zig_exe
    zig_exe=$(ls -d "${LOCALAPPDATA}"/Microsoft/WinGet/Packages/zig.zig*/zig*/zig.exe 2>/dev/null | head -1)
    if [ -n "$zig_exe" ]; then
        CC_CMD=("$zig_exe" cc)
        return
    fi
    local candidate
    for candidate in cc gcc clang; do
        if command -v "$candidate" >/dev/null 2>&1; then
            CC_CMD=("$candidate")
            return
        fi
    done
}

detect_cc

if [ ${#CC_CMD[@]} -eq 0 ]; then
    echo "ERROR: no C compiler found. Install one with:  winget install zig.zig" >&2
    echo "       (plain clang on Windows also needs the MSVC/Windows SDK headers)" >&2
    exit 1
fi

echo "Using compiler: ${CC_CMD[*]}"
mkdir -p build

FLAGS=(-std=c11 -Wall -Wextra -Werror -g -I../fap -I.)

build_and_run() {
    local name=$1
    shift
    # Skip suites whose test file does not exist yet, so the runner stays
    # usable while the core is still being built out task by task.
    if [ ! -f "$1" ]; then
        echo
        echo "=== $name (skipped, $1 not present) ==="
        return
    fi
    echo
    echo "=== $name ==="
    "${CC_CMD[@]}" "${FLAGS[@]}" "$@" -o "build/$name"
    "./build/$name"
}

build_and_run test_base64 test_base64.c ../fap/km_base64.c
build_and_run test_layout test_layout.c ../fap/km_layout.c

echo
echo "All host tests passed."
