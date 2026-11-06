#!/usr/bin/env bash
# Builds the Catch2 suite (vjn6_tests) and the benchmark (vjn6_bench) with emcc and runs both under Node
# (SPEC §13 "the same test binary compiled with emcc", §9.1 WASM benchmark, AC-N1.1).
#
#   web/build_tests.sh                 build, run the suite, run the benchmark
#   web/build_tests.sh --tests-only    skip the benchmark
#   web/build_tests.sh --bench-only    skip the suite
#   web/build_tests.sh -- <args>       pass Catch2 arguments (test names, tags, --reporter ...)
#
# Same compile flags as the web module (-O3 -msimd128, -ffp-contract=off, no fast-math) plus native
# Wasm exceptions for Catch2. NODERAWFS lets the tests read repository files (tests/golden/...).
# Build directory: build-wasm/ (override with VJN6_WASM_BUILD_DIR); Node: $NODE or node on PATH.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${VJN6_WASM_BUILD_DIR:-$ROOT/build-wasm}"
NODE_BIN="${NODE:-node}"

RUN_TESTS=1
RUN_BENCH=1
CATCH_ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --tests-only) RUN_BENCH=0 ;;
    --bench-only) RUN_TESTS=0 ;;
    --) shift; CATCH_ARGS=("$@"); break ;;
    *) echo "web/build_tests.sh: unknown option $1" >&2; exit 2 ;;
  esac
  shift
done

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
  echo "web/build_tests.sh: emcc not found. Install emsdk and run 'source <emsdk>/emsdk_env.sh'." >&2
  exit 1
fi

GENERATOR=()
if command -v ninja >/dev/null 2>&1; then GENERATOR=(-G Ninja); fi

CFLAGS="-msimd128 -fwasm-exceptions"
LDFLAGS="-fwasm-exceptions -sNODERAWFS=1 -sEXIT_RUNTIME=1 -sSTACK_SIZE=8MB -sINITIAL_MEMORY=64MB \
-sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=4GB"

emcmake cmake -S "$ROOT" -B "$BUILD_DIR" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVJN6_BUILD_TESTS=ON -DVJN6_BUILD_PLUGIN=OFF -DVJN6_BUILD_WEB=OFF \
  "-DCMAKE_C_FLAGS=$CFLAGS" "-DCMAKE_CXX_FLAGS=$CFLAGS" "-DCMAKE_EXE_LINKER_FLAGS=$LDFLAGS"

TARGETS=()
[ "$RUN_TESTS" = 1 ] && TARGETS+=(vjn6_tests)
[ "$RUN_BENCH" = 1 ] && TARGETS+=(vjn6_bench)
cmake --build "$BUILD_DIR" --target "${TARGETS[@]}" --parallel

echo "Node: $("$NODE_BIN" --version)"
status=0
if [ "$RUN_TESTS" = 1 ]; then
  (cd "$ROOT" && "$NODE_BIN" "$BUILD_DIR/tests/vjn6_tests.js" ${CATCH_ARGS[@]+"${CATCH_ARGS[@]}"}) || status=1
fi
if [ "$RUN_BENCH" = 1 ]; then
  (cd "$ROOT" && "$NODE_BIN" "$BUILD_DIR/tools/bench/vjn6_bench.js") || status=1
fi
exit $status
