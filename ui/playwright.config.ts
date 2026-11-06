// Playwright end-to-end tests (SPEC §13 intro: UI criteria run against the web build in Chromium and
// WebKit; test names contain the AC IDs). Run with `npm run test:e2e` after `web/build.sh`.
//
// Two dev servers: the normal one (`npm run web` behaviour, COOP/COEP, http://localhost:5173) and one
// without the isolation headers for AC-SC2 (VJN6_NO_ISOLATION=1, port 5174). Locally an already running
// server is reused; e2e/global-setup.ts refuses a port that serves another app. VJN6_E2E_PORT and
// VJN6_E2E_NOISO_PORT move the servers when those ports are taken (AC-B2 then reports the moved origin).
import { defineConfig, devices } from '@playwright/test';
import { NOISO_PORT, PORT } from './e2e/env';

const CI = !!process.env.CI;

// The canvas at 100 % (§5.1): with this viewport the Fit scale is exactly 1.
const viewport = { width: 1400, height: 560 };

export default defineConfig({
  testDir: './e2e',
  globalSetup: './e2e/global-setup.ts',
  timeout: 60_000,
  expect: { timeout: 5_000 },
  fullyParallel: true,
  forbidOnly: CI,
  retries: CI ? 1 : 0,
  workers: CI ? 2 : 4,
  reporter: CI ? [['list'], ['html', { open: 'never' }]] : [['list']],
  // AC-SC1 baselines: Chromium on Linux only (e2e/update-baselines.sh generates them in Docker).
  snapshotPathTemplate: '{testDir}/__screenshots__/{arg}-{projectName}-{platform}{ext}',
  use: {
    baseURL: `http://localhost:${PORT}`,
    viewport,
    deviceScaleFactor: 1,
    trace: 'retain-on-failure',
  },
  projects: [
    // Node-side checks of the built bundle (AC-SC3): no browser.
    { name: 'node', testMatch: /.*\.node\.spec\.ts$/ },
    {
      name: 'chromium',
      testIgnore: /.*\.node\.spec\.ts$/,
      use: { ...devices['Desktop Chrome'], viewport, deviceScaleFactor: 1 },
    },
    {
      name: 'webkit',
      testIgnore: /.*\.node\.spec\.ts$/,
      use: { ...devices['Desktop Safari'], viewport, deviceScaleFactor: 1 },
    },
  ],
  webServer: [
    {
      command: `npx vite --port ${PORT} --strictPort`,
      url: `http://localhost:${PORT}/`,
      reuseExistingServer: !CI,
      timeout: 60_000,
    },
    {
      command: `npx vite --port ${NOISO_PORT} --strictPort`,
      url: `http://localhost:${NOISO_PORT}/`,
      env: { VJN6_NO_ISOLATION: '1' },
      reuseExistingServer: !CI,
      timeout: 60_000,
    },
  ],
});
