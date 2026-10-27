#!/usr/bin/env bash
# Builds the portable core, runs its native tests, builds the host simulator and
# runs the end-to-end HTTP suite (fresh data directory each run).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

# MSYS2's cc1plus needs its DLL directory on PATH; without this g++ fails with
# exit 127 and no diagnostic.
export PATH="/c/msys64/mingw64/bin:/c/msys64/usr/bin:$PATH"

mkdir -p .build

echo "== core tests =="
g++ -std=c++11 -Wall -Wextra -I firmware/include -I firmware/lib/sntch_core/include \
  firmware/test/core_tests.cpp firmware/lib/sntch_core/src/*.cpp \
  -o .build/core_tests.exe
./.build/core_tests.exe

echo
echo "== host simulator =="
g++ -std=c++17 -Wall -Wextra -I firmware/lib/sntch_core/include \
  tools/host_sim/main.cpp firmware/lib/sntch_core/src/*.cpp \
  -o .build/host_sim.exe -lws2_32

echo
echo "== end-to-end =="
python tools/e2e_test.py
