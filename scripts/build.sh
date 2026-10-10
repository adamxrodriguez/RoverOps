#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if command -v cmake >/dev/null 2>&1; then
  cmake_bin="$(command -v cmake)"
elif [[ -x .tools/bin/cmake ]]; then
  cmake_bin="$PWD/.tools/bin/cmake"
else
  echo 'CMake 3.24+ is required. See README.md for installation.' >&2
  exit 1
fi
ctest_bin="$(dirname "$cmake_bin")/ctest"
build_dir="${BUILD_DIR:-build}"
"$cmake_bin" -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Debug "$@"
"$cmake_bin" --build "$build_dir" --parallel "${BUILD_JOBS:-4}"
"$ctest_bin" --test-dir "$build_dir" --output-on-failure
