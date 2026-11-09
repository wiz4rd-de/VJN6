#!/usr/bin/env bash
# Builds the VJN6 web module (SPEC §15.2): emcc -O3 -msimd128 -sAUDIO_WORKLET -sWASM_WORKERS
# -sENVIRONMENT=web,worker,worklet over vjn6_core + web/worklet.cpp, writing ui/public/vjn6.js (MODULARIZE,
# EXPORT_NAME=createVjn6; also the AudioWorklet module) and ui/public/vjn6.wasm.
#
# Needs an activated emsdk (emcc on PATH); if emcc is missing, $EMSDK/emsdk_env.sh or ~/emsdk/emsdk_env.sh
# is sourced. Build directory: build-web/ (override with VJN6_WEB_BUILD_DIR).
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VJN6_WEB_BUILD_DIR:-$ROOT/build-web}"

if ! command -v emcc >/dev/null 2>&1; then
  for env in "${EMSDK:-}/emsdk_env.sh" "$HOME/emsdk/emsdk_env.sh"; do
    if [ -f "$env" ]; then
      # shellcheck disable=SC1090
      source "$env" >/dev/null 2>&1
      break
    fi
  done
fi
if ! command -v emcc >/dev/null 2>&1; then
  echo "web/build.sh: emcc not found. Install emsdk and run 'source <emsdk>/emsdk_env.sh'." >&2
  exit 1
fi

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then GENERATOR=(-G Ninja); fi

emcmake cmake -S "$ROOT" -B "$BUILD_DIR" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVJN6_BUILD_WEB=ON -DVJN6_BUILD_PLUGIN=OFF -DVJN6_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --target vjn6_web --parallel

for f in vjn6.js vjn6.wasm; do
  if [ ! -s "$ROOT/ui/public/$f" ]; then
    echo "web/build.sh: ui/public/$f was not produced" >&2
    exit 1
  fi
done
echo "web/build.sh: wrote ui/public/vjn6.js and ui/public/vjn6.wasm"
