// AC-U1.1: mouse, wheel, typed entry and keyboard on sliders, knobs, TUNE, the 106 HPF slider, switches
// and the lever (R-U1, R-U1a). Drag distances are logical canvas px: CSS px ÷ the canvas scale.
import type { Locator, Page } from '@playwright/test';
import {
  canvasScale,
  centre,
  ctl,
  expect,
  F,
  openAndStart,
  P,
  setMode,
  state,
  test,
  waitForState,
  type CoreState,
} from './fixtures';

type Bound = { field: number } | { perf: number };

const read = (s: CoreState, b: Bound) => ('field' in b ? s.edit[b.field] : s.perf[b.perf]);

async function value(page: Page, b: Bound): Promise<number> {
  return read(await state(page), b);
}

async function expectValue(page: Page, loc: Locator, b: Bound, want: number | ((v: number) => boolean), what: string) {
  const ok = typeof want === 'number' ? (v: number) => v === want : want;
  const s = await waitForState(page, (st) => ok(read(st, b)), what);
  await expect(loc).toHaveAttribute('data-value', String(read(s, b)));
  return read(s, b);
}

/** Vertical drag of `logical` canvas px (up = positive), optionally with Ctrl held while moving. */
async function drag(page: Page, loc: Locator, logical: number, opts: { fine?: boolean } = {}): Promise<void> {
  const scale = await canvasScale(page);
  const c = await centre(loc);
  await page.mouse.move(c.x, c.y);
  await page.mouse.down();
  if (opts.fine) await page.keyboard.down('Control');
  await page.mouse.move(c.x, c.y - logical * scale, { steps: 8 });
  await page.mouse.up();
  if (opts.fine) await page.keyboard.up('Control');
}

async function wheel(page: Page, loc: Locator, shift = false): Promise<void> {
  const c = await centre(loc);
  await page.mouse.move(c.x, c.y);
  if (shift) await page.keyboard.down('Shift');
  await page.mouse.wheel(0, -100); // one notch up
  if (shift) await page.keyboard.up('Shift');
}

/** Double-click to type (R-U1); `key` Enter commits, Escape reverts. */
async function typeValue(page: Page, loc: Locator, text: string, key: 'Enter' | 'Escape' = 'Enter'): Promise<void> {
  await loc.dblclick();
  const entry = page.getByTestId('typed-entry');
  await expect(entry).toBeVisible();
  await expect(entry).toBeFocused();
  await entry.fill(text);
  await entry.press(key);
  await expect(entry).toHaveCount(0);
}

/** The whole R-U1 sequence on one slider or knob, from 0. */
async function exercise(page: Page, id: string, b: Bound): Promise<void> {
  const loc = ctl(page, id);
  await loc.focus();
  await page.keyboard.press('Home');
  await expectValue(page, loc, b, 0, `${id}: Home → 0`);
  await page.mouse.move(1, 1);

  await drag(page, loc, 20);
  await expectValue(page, loc, b, 10, `${id}: drag 20 px → +10`);
  const fine = await (async () => {
    await drag(page, loc, 20, { fine: true });
    return expectValue(page, loc, b, (v) => v === 12 || v === 13, `${id}: Ctrl-drag 20 px → +2…3`);
  })();
  await wheel(page, loc);
  await expectValue(page, loc, b, fine + 1, `${id}: wheel → +1`);
  await wheel(page, loc, true);
  await expectValue(page, loc, b, fine + 11, `${id}: Shift-wheel → +10`);
  // hovering shows the tooltip and never changes the value
  await expect(page.getByTestId('tooltip')).toBeVisible();
  expect(await value(page, b)).toBe(fine + 11);

  if (id === 'tune') {
    await typeValue(page, loc, '100');
    await expectValue(page, loc, b, 127, `${id}: typed 100 cents → clamps to 127`);
  } else {
    await typeValue(page, loc, '12');
    await expectValue(page, loc, b, 127, `${id}: typed 12 → clamps to 127`);
  }
  await typeValue(page, loc, 'abc');
  await page.waitForTimeout(100);
  expect(await value(page, b), `${id}: typed abc reverts`).toBe(127);
  await typeValue(page, loc, '5', 'Escape');
  await page.waitForTimeout(100);
  expect(await value(page, b), `${id}: Esc reverts`).toBe(127);
  await page.mouse.move(1, 1);
}

const SLIDERS_60: [string, number][] = [
  ['lfo-rate', F.LFO_RATE],
  ['lfo-delay', F.LFO_DELAY],
  ['dco-lfo', F.DCO_LFO],
  ['dco-pwm', F.DCO_PWM],
  ['sub-level', F.SUB_LEVEL],
  ['noise', F.NOISE],
  ['vcf-freq', F.VCF_FREQ],
  ['vcf-res', F.VCF_RES],
  ['vcf-env', F.VCF_ENV],
  ['vcf-lfo', F.VCF_LFO],
  ['vcf-kybd', F.VCF_KYBD],
  ['vca-level', F.VCA_LEVEL],
  ['env-a', F.ENV_A],
  ['env-d', F.ENV_D],
  ['env-s', F.ENV_S],
  ['env-r', F.ENV_R],
];

test.describe('AC-U1.1 sliders and knobs', () => {
  test('AC-U1.1 every slider: drag 20 px +10, Ctrl-drag +2–3, wheel +1, Shift-wheel +10, typed 12 → 127, abc reverts', async ({
    page,
  }) => {
    await openAndStart(page);
    for (const [id, field] of SLIDERS_60) await exercise(page, id, { field });
  });

  test('AC-U1.1 every knob (60 and 106): the same sequence; TUNE typed 10 → byte 77 (+10.2 cents)', async ({ page }) => {
    await openAndStart(page);
    for (const [id, perf] of [
      ['volume', P.VOLUME],
      ['bend-dco', P.BEND_DCO],
      ['bend-vcf', P.BEND_VCF],
      ['arp-rate', P.ARP_RATE],
      ['tune', P.TUNE],
    ] as const) {
      await exercise(page, id, { perf });
    }
    const tune = ctl(page, 'tune');
    await typeValue(page, tune, '10');
    await expectValue(page, tune, { perf: P.TUNE }, 77, 'TUNE typed 10 → 77');
    await expect(tune).toHaveAttribute('aria-valuetext', /^TUNE \+10\.2 = \+10\.2 cents$/);

    await setMode(page, '106');
    for (const [id, perf] of [
      ['lfo-trig-sens', P.LFO_TRIG_SENS],
      ['porta-time', P.PORTA_TIME],
    ] as const) {
      await exercise(page, id, { perf });
    }
  });

  test('AC-U1.1 drags are logical px at every scale (50 % and 200 %)', async ({ page }) => {
    await openAndStart(page);
    const b = { field: F.VCF_RES };
    const res = ctl(page, 'vcf-res');
    for (const [w, h, scale] of [
      [700, 280, 0.5],
      [2800, 1120, 2],
    ] as const) {
      await page.setViewportSize({ width: w, height: h });
      await expect(page.getByTestId('stage')).toHaveAttribute('data-scale', String(scale));
      await res.focus();
      await page.keyboard.press('Home');
      await expectValue(page, res, b, 0, 'RES 0');
      await drag(page, res, 20);
      await expectValue(page, res, b, 10, `scale ${scale}: drag 20 logical px → +10`);
      await drag(page, res, 20, { fine: true });
      await expectValue(page, res, b, (v) => v === 12 || v === 13, `scale ${scale}: Ctrl-drag → +2…3`);
      await drag(page, res, -20);
      await expectValue(page, res, b, (v) => v === 2 || v === 3, `scale ${scale}: drag down 20 → −10`);
    }
  });

  test('AC-U1.1 a focused slider: Up +1, Shift+Up +10, End 127, Down −1, Home 0', async ({ page }) => {
    await openAndStart(page);
    const env = ctl(page, 'vcf-env');
    const b = { field: F.VCF_ENV };
    expect(await value(page, b)).toBe(0);
    await env.focus();
    await page.keyboard.press('ArrowUp');
    await expectValue(page, env, b, 1, 'Up → +1');
    await page.keyboard.press('Shift+ArrowUp');
    await expectValue(page, env, b, 11, 'Shift+Up → +10');
    await page.keyboard.press('End');
    await expectValue(page, env, b, 127, 'End → 127');
    await page.keyboard.press('ArrowDown');
    await expectValue(page, env, b, 126, 'Down → −1');
    await page.keyboard.press('Home');
    await expectValue(page, env, b, 0, 'Home → 0');
  });

  test('AC-U1.1 106 HPF slider: typed 2 → HPF 2; drags snap to the 4 positions', async ({ page }) => {
    await openAndStart(page);
    await setMode(page, '106');
    const hpf = ctl(page, 'hpf');
    const b = { field: F.HPF };
    await typeValue(page, hpf, '2');
    await expectValue(page, hpf, b, 2, 'HPF typed 2');
    await expect(hpf).toHaveAttribute('aria-valuetext', /^HPF 2 = /);
    await drag(page, hpf, 20);
    await page.waitForTimeout(100);
    expect(await value(page, b), 'a short drag does not leave the position').toBe(2);
    await drag(page, hpf, 100); // one position is (300 − 14) / 3 ≈ 95 px
    await expectValue(page, hpf, b, 3, 'drag up one position → 3');
    await drag(page, hpf, -200);
    await expectValue(page, hpf, b, 1, 'drag down two positions → 1');
    await typeValue(page, hpf, '12');
    await expectValue(page, hpf, b, 3, 'typed 12 clamps to 3');
    await typeValue(page, hpf, 'abc');
    await page.waitForTimeout(100);
    expect(await value(page, b)).toBe(3);
  });
});

test('AC-U1.1 switches: a click on a position sets the field (labels and body)', async ({ page }) => {
  await openAndStart(page);
  await ctl(page, 'vca-mode-pos-1').click(); // GATE
  await expectValue(page, ctl(page, 'vca-mode'), { field: F.VCA_MODE }, 1, 'VCA GATE');
  await ctl(page, 'vca-mode-pos-0').click(); // ENV
  await expectValue(page, ctl(page, 'vca-mode'), { field: F.VCA_MODE }, 0, 'VCA ENV');
  await ctl(page, 'hpf-pos-2').click();
  await expectValue(page, ctl(page, 'hpf'), { field: F.HPF }, 2, 'HPF switch 2');
  await ctl(page, 'oct-transpose-pos-2').click(); // U
  await expectValue(page, ctl(page, 'oct-transpose'), { perf: P.OCT_TRANSPOSE }, 2, 'OCTAVE TRANSPOSE U');
  await expect(ctl(page, 'oct-transpose')).toHaveAttribute('aria-valuetext', 'U');
  await ctl(page, 'oct-transpose-pos-0').click(); // D
  await expectValue(page, ctl(page, 'oct-transpose'), { perf: P.OCT_TRANSPOSE }, 0, 'OCTAVE TRANSPOSE D');
  await expect(ctl(page, 'oct-transpose')).toHaveAttribute('aria-valuetext', 'D');

  // the switch body: each 16 px position from the top (PWM: LFO / MAN / ENV)
  const pwm = ctl(page, 'pwm-mode');
  const box = (await pwm.boundingBox())!;
  const scale = await canvasScale(page);
  for (const [pos, v] of [
    [2, 2],
    [1, 1],
    [0, 0],
  ] as const) {
    await page.mouse.click(box.x + box.width / 2, box.y + (pos * 16 + 8) * scale);
    await expectValue(page, pwm, { field: F.PWM_SRC }, v, `PWM switch position ${pos}`);
  }
  // focused switch: Up / Down move one position (R-U1a)
  await ctl(page, 'arp-mode').focus();
  await page.keyboard.press('ArrowDown');
  await expectValue(page, ctl(page, 'arp-mode'), { perf: P.ARP_MODE }, 1, 'ARP MODE U&D');
});

test.describe('AC-U1.1 lever', () => {
  test('AC-U1.1 lever: drag 30 px → +0.5, released outside the window → 0', async ({ page }) => {
    await openAndStart(page);
    const lever = ctl(page, 'lever');
    const box = (await lever.boundingBox())!;
    const scale = await canvasScale(page);
    const x0 = box.x + box.width / 2 - 20 * scale; // anywhere on it: full deflection at ±60 px from here
    const y0 = box.y + box.height * 0.75;
    await page.mouse.move(x0, y0);
    await page.mouse.down();
    await page.mouse.move(x0 + 30 * scale, y0, { steps: 6 });
    await expect(lever).toHaveAttribute('data-value', '4096');
    await waitForState(page, (s) => s.lever === 4096, 'lever +0.5 (round(0.5 × 8191))');
    await expect(lever).toHaveAttribute('aria-valuetext', 'BENDER 0.50');
    await page.mouse.move(x0 + 300 * scale, y0, { steps: 4 });
    await waitForState(page, (s) => s.lever === 8191, 'full deflection beyond 60 px');
    // released outside the window
    await page.mouse.move(-40, -40, { steps: 4 });
    await page.mouse.up();
    await waitForState(page, (s) => s.lever === 0, 'springs back to centre');
    await expect(lever).toHaveAttribute('data-value', '0');
    await expect(lever).toHaveAttribute('data-dragging', 'false');
  });

  test('AC-U1.1 lever (106): an upper-half press holds push until release', async ({ page }) => {
    await openAndStart(page);
    await setMode(page, '106');
    const lever = ctl(page, 'lever');
    const box = (await lever.boundingBox())!;
    const scale = await canvasScale(page);
    const x0 = box.x + box.width / 2;
    await page.mouse.move(x0, box.y + box.height * 0.25);
    await page.mouse.down();
    await waitForState(page, (s) => s.push, 'push held');
    await expect(lever).toHaveAttribute('data-push', 'true');
    await page.mouse.move(x0 - 30 * scale, box.y + box.height * 0.9, { steps: 4 });
    // −0.5 × 8191 = −4095.5: either neighbour (the UI rounds the drag to the protocol's integer)
    await waitForState(page, (s) => Math.abs(s.lever + 4095.5) === 0.5 && s.push, 'push does not change during the drag');
    await page.mouse.up();
    await waitForState(page, (s) => !s.push && s.lever === 0, 'release ends push and centres');
    await expect(lever).toHaveAttribute('data-push', 'false');
    // a lower-half press does not push
    await page.mouse.move(x0, box.y + box.height * 0.75);
    await page.mouse.down();
    await page.waitForTimeout(150);
    expect((await state(page)).push).toBe(false);
    await page.mouse.up();
  });
});
