#!/usr/bin/env bash
# Runs the SNTCH host simulator on port 8080 with data in .sim-data/.
# Use it to exercise the Android app without an ESP32-S3.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="/c/msys64/mingw64/bin:$PATH"

if [ ! -x .build/host_sim.exe ]; then
  mkdir -p .build
  g++ -std=c++17 -I firmware/lib/sntch_core/include \
    tools/host_sim/main.cpp firmware/lib/sntch_core/src/*.cpp \
    -o .build/host_sim.exe -lws2_32
fi

PORT="${1:-8080}"
echo "Controller simulator: http://127.0.0.1:$PORT"
echo "Teacher PIN: 246810, data: .sim-data/"
echo "For a phone on the same network use http://<this-PC-IP>:$PORT, not 127.0.0.1."
exec ./.build/host_sim.exe "$PORT" .sim-data
