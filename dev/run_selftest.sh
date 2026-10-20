#!/usr/bin/env bash
# Builds and runs the synthetic-ped logic tests with any recent g++ (Linux/macOS/MSYS2). No game needed.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${TMPDIR:-/tmp}/dhf_selftest"
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -Idev/winstub \
    dev/selftest.cpp src/Skeleton.cpp src/Reactions.cpp src/Log.cpp src/Config.cpp src/GameSA.cpp -o "$OUT"
"$OUT"
