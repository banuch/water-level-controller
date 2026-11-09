#!/usr/bin/env bash
# Builds and runs the hardware-independent unit tests with the host C++
# compiler, without PlatformIO. Equivalent to `pio test -e native`.
#
#   UNITY_DIR=/path/to/Unity ARDUINOJSON_DIR=/path/to/ArduinoJson tools/run_host_tests.sh
#
# If the variables are not set, pinned versions are cloned into .host-deps/.
set -euo pipefail
cd "$(dirname "$0")/.."

DEPS=.host-deps
UNITY_DIR=${UNITY_DIR:-$DEPS/Unity}
ARDUINOJSON_DIR=${ARDUINOJSON_DIR:-$DEPS/ArduinoJson}
if [ ! -f "$UNITY_DIR/src/unity.c" ]; then
  git clone -q --depth 1 -b v2.6.0 https://github.com/ThrowTheSwitch/Unity "$UNITY_DIR"
fi
if [ ! -f "$ARDUINOJSON_DIR/src/ArduinoJson.h" ]; then
  git clone -q --depth 1 -b v7.2.1 https://github.com/bblanchon/ArduinoJson "$ARDUINOJSON_DIR"
fi

CXX=${CXX:-g++}
OUT=.host-build
mkdir -p "$OUT"
CORE_SRC=(src/core/Types.cpp src/sensors/ProbeSensor.cpp src/sensors/LevelEstimator.cpp
          src/motor/SafetyManager.cpp src/motor/MotorStateMachine.cpp src/storage/Config.cpp src/storage/ConfigJson.cpp
          src/services/ScheduleManager.cpp src/services/CommandProcessor.cpp
          src/services/OtaManifest.cpp src/services/AlertManager.cpp)
FLAGS=(-std=gnu++17 -O1 -g -Wall -Wextra -Werror -Wno-type-limits -Wno-maybe-uninitialized -Isrc -Iinclude
       -I"$UNITY_DIR/src" -isystem "$ARDUINOJSON_DIR/src" -DUNITY_INCLUDE_DOUBLE -DUNITY_SUPPORT_64
       -fsanitize=address,undefined -fno-sanitize-recover=undefined)

"${CXX}" -x c -c "$UNITY_DIR/src/unity.c" -o "$OUT/unity.o" -I"$UNITY_DIR/src" \
  -DUNITY_INCLUDE_DOUBLE -DUNITY_SUPPORT_64

fail=0
for t in test/test_*/; do
  name=$(basename "$t")
  "${CXX}" "${FLAGS[@]}" "${CORE_SRC[@]}" "$t"test_main.cpp "$OUT/unity.o" -o "$OUT/$name"
  echo "== $name"
  if ! "$OUT/$name"; then fail=1; fi
done
exit $fail
