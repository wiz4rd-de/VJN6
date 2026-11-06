// AC-SC1: screenshots at 50 / 100 / 200 % in both modes, the bank manager and Settings, each within
// 0.1 % pixel difference of the committed baselines. The criterion is Chromium on Linux: the baselines
// (e2e/__screenshots__/*-chromium-linux.png) come from e2e/update-baselines.sh, which runs this file in
// the official Playwright Docker image; elsewhere the test is skipped.
import { expect, openApp, openBankManager, openSettings, setMode, startApp, test } from './fixtures';

const MAX_DIFF = { maxDiffPixelRatio: 0.001 }; // ≤ 0.1 % of the pixels

test.skip(({ browserName }) => browserName !== 'chromium', 'AC-SC1 is a Chromium criterion');
test.skip(process.platform !== 'linux', 'AC-SC1 baselines are Linux renders (npm run test:e2e:baselines)');

/** The page sized to the canvas at `scale` (Fit), started, the pointer parked on the end cheek. */
async function panelAt(page: import('@playwright/test').Page, scale: number, mode: '60' | '106') {
  await page.setViewportSize({ width: 1400 * scale, height: 560 * scale });
  await openApp(page);
  await startApp(page);
  if (mode === '106') await setMode(page, '106');
  await expect(page.getByTestId('stage')).toHaveAttribute('data-scale', String(scale));
  await page.mouse.move(1, 1);
  await expect(page.getByTestId('tooltip')).toHaveCount(0);
  await expect(page.getByTestId('led')).toHaveAttribute('data-text', '11');
}

for (const mode of ['60', '106'] as const) {
  for (const pct of [50, 100, 200]) {
    test(`AC-SC1 ${mode} mode at ${pct} %`, async ({ page }) => {
      await panelAt(page, pct / 100, mode);
      await expect(page).toHaveScreenshot(`panel-${mode}-${pct}.png`, MAX_DIFF);
    });
  }

  test(`AC-SC1 bank manager (${mode} mode, 100 %)`, async ({ page }) => {
    await panelAt(page, 1, mode);
    await openBankManager(page);
    await expect(page).toHaveScreenshot(`bank-manager-${mode}.png`, MAX_DIFF);
  });

  test(`AC-SC1 settings (${mode} mode, 100 %)`, async ({ page }) => {
    await panelAt(page, 1, mode);
    const dlg = await openSettings(page);
    await expect(dlg.getByTestId('set-midi-inputs')).toContainText('VJN6 Test Keys');
    await expect(page).toHaveScreenshot(`settings-${mode}.png`, MAX_DIFF);
  });
}
