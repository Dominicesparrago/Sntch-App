#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .build/dual
src=firmware/lib/sntch_core
panel=firmware/lib/sntch_panel/src
qr=firmware/lib/sntch_qr/src
node tools/generate_panel.mjs --check
cc -std=c99 -O2 -Wall -Wextra -c "$qr/qrcodegen.c" -o .build/dual/qr.o
flags=(-std=c++17 -O2 -Wall -Wextra '-DSNTCH_AP_PASSWORD="host-tests"' '-DSNTCH_TEACHER_PIN="58372649"'
  -I firmware/test/dual_host -I firmware/src -I firmware/include -I "$src/include" -I "$panel" -I "$qr")
c++ "${flags[@]}" -DSNTCH_DUAL_PANEL=1 -DSNTCH_SOLO_PANEL=1 \
  firmware/test/dual_host/main.cpp firmware/src/dual_panel.cpp firmware/src/solo_panel.cpp \
  "$src/src/classroom.cpp" "$src/src/json.cpp" .build/dual/qr.o -o .build/dual/tests
.build/dual/tests .build/dual

c++ "${flags[@]}" -DSNTCH_PHONE_ADMIN=1 -I firmware/phone_admin \
  firmware/test/dual_host/phone_admin.cpp firmware/phone_admin/*.cpp \
  "$src"/src/*.cpp .build/dual/qr.o -o .build/dual/phone_admin_tests
card=$(mktemp -d .build/dual/card.XXXXXX)
trap 'rm -rf -- "$card"' EXIT
.build/dual/phone_admin_tests "$card"
