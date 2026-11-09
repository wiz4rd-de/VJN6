#!/bin/sh
# AC-SC1 screenshot baselines (Chromium on Linux, SPEC §13.4). Runs e2e/screenshots.spec.ts inside the
# official Playwright image that matches ui/package.json's @playwright/test version, on a copy of the UI
# (the host's node_modules are never touched), and writes e2e/__screenshots__/*-chromium-linux.png.
#
#   npm run test:e2e:baselines            regenerate the baselines (only with an intended UI change)
#   npm run test:e2e:baselines -- --check compare against the committed baselines without updating
#
# Needs Docker and the web module (web/build.sh). The image runs as linux/amd64 like the CI runner
# (ubuntu-24.04 x64); VJN6_BASELINE_PLATFORM=linux/arm64 uses the native image on Apple Silicon.
set -eu

UI=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$UI/.." && pwd)
VERSION=$(node -p "require('$UI/package.json').devDependencies['@playwright/test']")
IMAGE="mcr.microsoft.com/playwright:v$VERSION-noble"
PLATFORM=${VJN6_BASELINE_PLATFORM:-linux/amd64}
MODE=update
if [ "${1:-}" = "--check" ]; then MODE=check; fi

for f in vjn6.js vjn6.wasm; do
  if [ ! -s "$UI/public/$f" ]; then
    echo "update-baselines: ui/public/$f is missing; run web/build.sh first" >&2
    exit 1
  fi
done
mkdir -p "$UI/e2e/__screenshots__"

echo "update-baselines: $MODE with $IMAGE ($PLATFORM)"
docker run --rm --platform "$PLATFORM" --ipc=host --init \
  -e CI=1 -e MODE="$MODE" \
  -v "$ROOT/package.json:/src/package.json:ro" \
  -v "$ROOT/package-lock.json:/src/package-lock.json:ro" \
  -v "$UI:/src/ui:ro" \
  -v "$UI/e2e/__screenshots__:/out" \
  "$IMAGE" bash -euc '
    mkdir -p /work/ui
    cp /src/package.json /src/package-lock.json /work/
    cd /src/ui
    tar --exclude=./node_modules --exclude=./dist --exclude=./test-results --exclude=./playwright-report \
      --exclude=./e2e/__screenshots__ -cf - . | tar -xf - -C /work/ui
    mkdir -p /work/ui/e2e/__screenshots__
    cp /out/*-chromium-linux.png /work/ui/e2e/__screenshots__/ 2>/dev/null || true
    cd /work
    npm ci --no-audit --no-fund --loglevel=error
    cd /work/ui
    if [ "$MODE" = update ]; then
      npx playwright test --project=chromium --grep "AC-SC1" --update-snapshots=all --reporter=list
      rm -f /out/*-chromium-linux.png
      cp e2e/__screenshots__/*-chromium-linux.png /out/
      ls -l /out
    else
      npx playwright test --project=chromium --grep "AC-SC1" --reporter=list
    fi
  '
