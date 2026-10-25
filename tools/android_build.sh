#!/usr/bin/env bash
# Runs the Android JVM tests and builds the debug APK.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/android"

# This machine's JAVA_HOME points at a missing jdk-21. Override if needed:
export JAVA_HOME="${SNTCH_JAVA_HOME:-C:/Program Files/Java/jdk-25}"

./gradlew :app:testDebugUnitTest :app:assembleDebug --console=plain
echo
echo "APK: $ROOT/android/app/build/outputs/apk/debug/app-debug.apk"
