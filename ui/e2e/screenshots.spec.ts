// AC-SC1: screenshots at 50 / 100 / 200 % in both modes, the bank manager and Settings, each within
// 0.1 % pixel difference of the committed baselines. The criterion is Chromium on Linux: the baselines
// (e2e/__screenshots__/*-chromium-linux.png) come from e2e/update-baselines.sh, which runs this file in
// the official Playwright Docker image; elsewhere the test is skipped.
//
// The panel and Settings shots show the layout with INIT on slot 1 (selected, CLEAN, LED `11`), so they
// do not depend on what the default bank holds in slot 1 (§15.3: a factory patch since v3.2); the
// bank-manager shots show the plain first-run state, whose list is the default bank's names.
import type { Page } from '@playwright/test';
import {
  bank,
  expect,
  openApp,
  openBankManager,
  openSettings,
  P,
  setMode,
  startApp,
  test,
  waitForState,
} from './fixtures';

const MAX_DIFF = { maxDiffPixelRatio: 0.001 }; // ≤ 0.1 % of the pixels

test.skip(({ browserName }) => browserName !== 'chromium', 'AC-SC1 is a Chromium criterion');
test.skip(process.platform !== 'linux', 'AC-SC1 baselines are Linux renders (npm run test:e2e:baselines)');

/**
 * Copies slot 57 (INIT, §15.3) onto slot 1 in the bank manager (memory protect off for the row drop,
 * back on afterwards, F-10) and selects slot 1 with Alt+1 (PATCH 1 with the pending bank 1, R-U2), all
 * through the app's own UI. The keyboard selection leaves no panel control focused or hovered.
 */
async function initOnSlot1(page: Page): Promise<void> {
  const dlg = await openBankManager(page);
  await expect(dlg.getByTestId('bank-name-57')).toHaveText('INIT');
  const protect = dlg.getByTestId('bm-protect');
  await protect.click();
  await waitForState(page, (s) => s.perf[P.PROTECT] === 0);
  // a row drop with the bank manager's own drag data (rows 57 and 1 are never both in view)
  const dt = await page.evaluateHandle(() => new DataTransfer());
  await dlg.getByTestId('bank-slot-57').dispatchEvent('dragstart', { dataTransfer: dt });
  await dlg.getByTestId('bank-row-1').dispatchEvent('dragover', { dataTransfer: dt });
  await dlg.getByTestId('bank-row-1').dispatchEvent('drop', { dataTransfer: dt });
  await expect(dlg.getByTestId('bank-name-1')).toHaveText('INIT');
  const b = await bank(page);
  expect(b[0]).toEqual(b[56]);
  await protect.click();
  await waitForState(page, (s) => s.perf[P.PROTECT] === 1);
  // closed with a click outside it, not Esc: after a plain key press Chromium draws the focus ring on the
  // control an overlay focuses when it opens, which the first-run flow (mouse and Alt shortcuts) never does
  await page.getByTestId('overlay-backdrop').click({ position: { x: 5, y: 5 } });
  await expect(dlg).toHaveCount(0);
  await page.keyboard.down('Alt');
  await page.keyboard.press('Digit1');
  await page.keyboard.up('Alt');
  await waitForState(
    page,
    (s) => s.slot === 1 && s.name === 'INIT' && !s.manual && !s.edited && s.pendingBank === 1 && s.led.message === 0,
  );
}

/** The page sized to the canvas at `scale` (Fit), started, the pointer parked on the end cheek. */
async function panelAt(page: Page, scale: number, mode: '60' | '106', slot1: 'init' | 'first-run') {
  await page.setViewportSize({ width: 1400 * scale, height: 560 * scale });
  await openApp(page);
  await startApp(page);
  if (slot1 === 'init') await initOnSlot1(page);
  if (mode === '106') await setMode(page, '106');
  await expect(page.getByTestId('stage')).toHaveAttribute('data-scale', String(scale));
  await page.mouse.move(1, 1);
  await expect(page.getByTestId('tooltip')).toHaveCount(0);
  await expect(page.getByTestId('led')).toHaveAttribute('data-text', '11');
}

for (const mode of ['60', '106'] as const) {
  for (const pct of [50, 100, 200]) {
    test(`AC-SC1 ${mode} mode at ${pct} %`, async ({ page }) => {
      await panelAt(page, pct / 100, mode, 'init');
      await expect(page).toHaveScreenshot(`panel-${mode}-${pct}.png`, MAX_DIFF);
    });
  }

  test(`AC-SC1 bank manager (${mode} mode, 100 %)`, async ({ page }) => {
    await panelAt(page, 1, mode, 'first-run');
    await openBankManager(page);
    await expect(page).toHaveScreenshot(`bank-manager-${mode}.png`, MAX_DIFF);
  });

  test(`AC-SC1 settings (${mode} mode, 100 %)`, async ({ page }) => {
    await panelAt(page, 1, mode, 'init');
    const dlg = await openSettings(page);
    await expect(dlg.getByTestId('set-midi-inputs')).toContainText('VJN6 Test Keys');
    await expect(page).toHaveScreenshot(`settings-${mode}.png`, MAX_DIFF);
  });
}
