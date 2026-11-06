// AC-S3.2 (a reloaded page shows exactly the state before) and the web part of AC-F13.1 (state saved to
// IndexedDB survives a reload within 1 s; without IndexedDB the banner shows and VJN6 keeps running).
import type { Page } from '@playwright/test';
import {
  bank,
  ctl,
  expect,
  F,
  openApp,
  openAndStart,
  waitForApp,
  P,
  setMode,
  setProtect,
  startApp,
  state,
  test,
  waitForState,
} from './fixtures';

/** Everything the panel displays: every control's value and LED, the LED display and the mode badge. */
async function displayed(page: Page): Promise<Record<string, string | null>> {
  return page.evaluate(() => {
    const out: Record<string, string | null> = {};
    for (const el of document.querySelectorAll('[data-testid^="ctl-"]')) {
      const id = el.getAttribute('data-testid')!;
      for (const a of ['data-value', 'data-lit', 'data-blinking', 'data-push', 'aria-valuetext', 'aria-pressed']) {
        if (el.hasAttribute(a)) out[`${id} ${a}`] = el.getAttribute(a);
      }
      if (id.includes('-pos-')) out[`${id} class`] = el.getAttribute('class');
    }
    const led = document.querySelector('[data-testid="led"]')!;
    for (const a of ['data-text', 'data-raw-text', 'data-dp', 'data-blink', 'data-message', 'data-slot-tooltip']) {
      out[`led ${a}`] = led.getAttribute(a);
    }
    out['mode-badge'] = document.querySelector('[data-testid="mode-badge"]')!.textContent;
    out['panel mode'] = document.querySelector('[data-testid="panel"]')!.getAttribute('data-mode');
    return out;
  });
}

async function typeInto(page: Page, id: string, text: string): Promise<void> {
  await ctl(page, id).dblclick();
  await page.getByTestId('typed-entry').fill(text);
  await page.getByTestId('typed-entry').press('Enter');
  await expect(page.getByTestId('typed-entry')).toHaveCount(0);
}

test('AC-S3.2 a non-default state: after a page reload every displayed value, the LED and the button LEDs are as before', async ({
  page,
}) => {
  await openAndStart(page);
  await setMode(page, '106');
  // slot B34 (= 84), then edits: EDITED with decimal points
  await ctl(page, 'group-b').click();
  await ctl(page, 'bank-3').click();
  await ctl(page, 'patch-4').click();
  await waitForState(page, (s) => s.slot === 84 && !s.edited);
  await typeInto(page, 'vcf-freq', '6.3');
  await typeInto(page, 'vcf-res', '2.5');
  await typeInto(page, 'env-a', '1');
  await typeInto(page, 'hpf', '3');
  await ctl(page, 'pwm-mode-pos-1').click();
  await ctl(page, 'vca-mode-pos-1').click();
  // buttons with LEDs, knobs and performance settings
  await ctl(page, 'range-4').click();
  await ctl(page, 'pulse').click();
  await ctl(page, 'chorus-ii').click();
  await ctl(page, 'portamento').click();
  await ctl(page, 'poly-2').click();
  await typeInto(page, 'tune', '10');
  await typeInto(page, 'porta-time', '7');
  await typeInto(page, 'bend-dco', '3');
  // key transpose +4: arm, then E (computer key D) sets it; the KEY TRANSPOSE LED stays lit
  await ctl(page, 'key-transpose').click();
  await waitForState(page, (s) => s.ktArmed);
  await page.mouse.move(1, 1);
  await page.keyboard.press('KeyD');
  const before = await waitForState(
    page,
    (s) =>
      s.perf[P.KEY_TRANSPOSE] === 4 &&
      !s.ktArmed &&
      s.edited &&
      s.edit[F.CHORUS] === 2 &&
      s.perf[P.KEY_MODE] === 1 &&
      s.perf[P.TUNE] === 77,
  );
  expect(before.slot).toBe(84);
  await expect(page.getByTestId('led')).toHaveAttribute('data-text', '34');
  await expect(page.getByTestId('led')).toHaveAttribute('data-dp', 'true');
  await expect(ctl(page, 'key-transpose')).toHaveAttribute('data-lit', 'true');
  await expect(ctl(page, 'group-b')).toHaveAttribute('data-lit', 'true');

  await page.waitForTimeout(1600); // the save runs 1 s after the last change (F-13)
  const shown = await displayed(page);
  expect(Object.keys(shown).length).toBeGreaterThan(100);

  await page.reload();
  await waitForApp(page);
  await startApp(page);
  await expect.poll(() => displayed(page)).toEqual(shown);
  const after = await state(page);
  for (const k of ['mode', 'edit', 'name', 'stored', 'edited', 'manual', 'slot', 'pendingBank', 'pendingGroup', 'lit'] as const) {
    expect(after[k], k).toEqual(before[k]);
  }
  expect(after.perf.slice(0, 16)).toEqual(before.perf.slice(0, 16));
});

test.describe('AC-F13.1 web persistence', () => {
  test('AC-F13.1 web: a slider edit and a WRITE, then a reload within 1 s → the same state returns', async ({ page }) => {
    await openAndStart(page);
    await setProtect(page, false);
    await ctl(page, 'patch-2').click(); // BRASS
    const brass = await waitForState(page, (s) => s.slot === 2 && s.name === 'BRASS');
    await typeInto(page, 'vcf-freq', '5');
    await waitForState(page, (s) => s.edit[F.VCF_FREQ] === 64 && s.edited);
    expect(brass.edit[F.VCF_FREQ]).not.toBe(64);
    await ctl(page, 'write').click();
    await ctl(page, 'patch-5').click();
    const written = await waitForState(page, (s) => s.slot === 5 && !s.writeArmed && !s.edited);
    const t0 = Date.now();
    await expect(page.getByTestId('led')).toHaveAttribute('data-raw-text', 'ok');
    const bankBefore = await bank(page);
    expect(bankBefore[4]).toEqual({ n: 'BRASS', p: written.edit });

    await page.reload();
    const reloadedAfter = Date.now() - t0;
    expect(reloadedAfter, 'the reload starts within 1 s of the WRITE').toBeLessThan(1000);
    await waitForApp(page);
    const s = await state(page);
    expect(s.slot).toBe(5);
    expect(s.edit).toEqual(written.edit);
    expect(s.edit[F.VCF_FREQ]).toBe(64);
    expect(s.name).toBe('BRASS');
    expect(s.edited).toBe(false);
    expect(s.manual).toBe(false);
    expect(s.perf[P.PROTECT]).toBe(0);
    const bankAfter = await bank(page);
    expect(bankAfter[4]).toEqual({ n: 'BRASS', p: written.edit });
    expect(bankAfter).toEqual(bankBefore);
    await startApp(page);
    await expect(page.getByTestId('led')).toHaveAttribute('data-text', '15');
    await expect(page.getByTestId('banner-storage')).toHaveCount(0);
  });

  test('AC-F13.1 web: reloads in quick succession (a page closed while it starts) keep the saved state', async ({
    page,
  }) => {
    // every slot the UI shows, frame by frame, from the first frame of each page load
    await page.addInitScript(() => {
      const seen: number[] = [];
      (window as unknown as { __slots: number[] }).__slots = seen;
      const tick = () => {
        const s = window.__vjn6Test?.state();
        if (s && seen[seen.length - 1] !== s.slot) seen.push(s.slot);
        requestAnimationFrame(tick);
      };
      requestAnimationFrame(tick);
    });
    await openAndStart(page);
    await setProtect(page, false);
    await ctl(page, 'patch-2').click();
    await waitForState(page, (s) => s.slot === 2);
    await ctl(page, 'write').click();
    await ctl(page, 'patch-6').click();
    const written = await waitForState(page, (s) => s.slot === 6 && !s.writeArmed);
    await page.waitForTimeout(1500); // saved by the 1 s timer

    for (const delay of [0, 15, 30, 50, 75, 100, 150, 200]) {
      await page.reload({ waitUntil: 'commit' });
      if (delay) await page.waitForTimeout(delay);
      await page.reload();
      await waitForApp(page);
      await page.waitForTimeout(150);
      const slots = await page.evaluate(() => (window as unknown as { __slots: number[] }).__slots);
      expect(slots, `after a reload ${delay} ms into the previous load, the UI shows only the saved slot`).toEqual([6]);
      const s = await state(page);
      expect(s.edit).toEqual(written.edit);
      expect(s.name).toBe('BRASS');
    }
  });

  test.describe('IndexedDB disabled', () => {
    test.beforeEach(async ({ page }) => {
      await page.addInitScript(() => {
        Object.defineProperty(window, 'indexedDB', { configurable: true, get: () => undefined });
      });
    });

    test('AC-F13.1 web: IndexedDB disabled → the banner shows and VJN6 keeps running', async ({ page }) => {
      await openApp(page);
      expect(await page.evaluate(() => typeof window.indexedDB)).toBe('undefined');
      const banner = page.getByTestId('banner-storage');
      await expect(banner).toBeVisible();
      await expect(banner).toHaveText('Changes will not be saved in this browser.');
      await startApp(page);
      await ctl(page, 'pulse').click();
      await waitForState(page, (s) => s.edit[F.PULSE] === 1 && s.edited);
      await expect(page.getByTestId('led')).toHaveAttribute('data-dp', 'true');
    });
  });

  test.describe('IndexedDB writes fail', () => {
    test.beforeEach(async ({ page }) => {
      await page.addInitScript(() => {
        IDBObjectStore.prototype.put = function () {
          throw new DOMException('quota exceeded (test)', 'QuotaExceededError');
        };
      });
    });

    test('AC-F13.1 web: a failed IndexedDB write → the banner shows and VJN6 keeps running', async ({ page }) => {
      await openAndStart(page);
      await expect(page.getByTestId('banner-storage')).toHaveCount(0);
      await ctl(page, 'saw').click();
      await waitForState(page, (s) => s.edit[F.SAW] === 0);
      await expect(page.getByTestId('banner-storage')).toBeVisible({ timeout: 4_000 });
      await ctl(page, 'saw').click();
      await waitForState(page, (s) => s.edit[F.SAW] === 1);
    });
  });
});
