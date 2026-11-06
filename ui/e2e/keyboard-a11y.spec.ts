// AC-U2.1 (R-U2 shortcuts, no note sounds) and AC-U3.1 (Tab order, focus ring, names; the contrast part
// of AC-U3.1 is the Vitest test ui/tests/contrast.test.ts).
import AxeBuilder from '@axe-core/playwright';
import type { Page } from '@playwright/test';
import {
  closeOverlay,
  ctl,
  expect,
  openAndStart,
  openBankManager,
  openSettings,
  peakRmsOver,
  setMode,
  SOUND_DB,
  state,
  test,
  waitForRms,
  waitForState,
} from './fixtures';

// ---------------------------------------------------------------- AC-U2.1

/** Holds Alt+`code` for a moment and checks that no note sounds and no keyboard function acts. */
async function shortcut(page: Page, code: string): Promise<void> {
  await page.keyboard.down('Alt');
  await page.keyboard.down(code);
  const peak = await peakRmsOver(page, 250);
  await page.keyboard.up(code);
  await page.keyboard.up('Alt');
  const after = await peakRmsOver(page, 150);
  expect(Math.max(peak, after), `Alt+${code} plays no note`).toBeLessThan(SOUND_DB);
  const s = await state(page);
  expect(s.lever, `Alt+${code} does not move the lever`).toBe(0);
  expect(s.push).toBe(false);
  expect(s.lfoTrig).toBe(false);
  const kb = await page.evaluate(() => window.__vjn6Test!.keyboard());
  expect(kb.play, `Alt+${code} does not toggle keyboard play`).toBe(true);
}

test('AC-U2.1 every R-U2 shortcut acts and no note sounds', async ({ page }) => {
  await openAndStart(page, { audio: true });
  // chorus hiss would mask "silent" on the patches with chorus: switch it off (§4.4) for this check
  const dlg = await openSettings(page);
  await dlg.getByTestId('set-chorus-noise-0').click();
  await waitForState(page, (s) => s.settings[1] === 0);
  await closeOverlay(page);
  await waitForRms(page, 'below', SOUND_DB);

  // Alt+B: bank manager (toggles); Esc closes an overlay
  await shortcut(page, 'KeyB');
  await expect(page.getByTestId('bank-manager')).toBeVisible();
  await shortcut(page, 'KeyB');
  await expect(page.getByTestId('bank-manager')).toHaveCount(0);
  await shortcut(page, 'KeyB');
  await expect(page.getByTestId('bank-manager')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.getByTestId('bank-manager')).toHaveCount(0);

  // Alt+, : settings
  await shortcut(page, 'Comma');
  await expect(page.getByTestId('settings')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.getByTestId('settings')).toHaveCount(0);

  // Alt+1…8: PATCH 1–8 with the pending bank (bank 1)
  for (let i = 1; i <= 8; i++) {
    await shortcut(page, `Digit${i}`);
    await waitForState(page, (s) => s.slot === i && !s.manual, `Alt+${i} selects slot ${i}`);
    await expect(page.getByTestId('led')).toHaveAttribute('data-text', `1${i}`);
  }
  await ctl(page, 'bank-3').click();
  await shortcut(page, 'Digit2');
  await waitForState(page, (s) => s.slot === 18, 'Alt+2 with pending bank 3 selects slot 18');

  // Alt+M: Manual
  await shortcut(page, 'KeyM');
  await waitForState(page, (s) => s.manual, 'Alt+M → Manual');
  await expect(page.getByTestId('led')).toHaveAttribute('data-text', '--');

  // Alt+W: WRITE arms (blinks); again cancels
  await shortcut(page, 'KeyW');
  await waitForState(page, (s) => s.writeArmed, 'Alt+W arms WRITE');
  await expect(ctl(page, 'write')).toHaveAttribute('data-blinking', 'true');
  await shortcut(page, 'KeyW');
  await waitForState(page, (s) => !s.writeArmed, 'Alt+W again cancels');

  // the same keys without Alt do play (the silence above is not a dead output)
  await page.keyboard.down('KeyW');
  await waitForRms(page, 'above', SOUND_DB);
  await page.keyboard.up('KeyW');
});

// ---------------------------------------------------------------- AC-U3.1

/** §5.2 boxes, in R-U3 order: the sections left to right, then the memory row, then the right edge. */
const BOXES: [string, number, number, number, number][] = [
  ['controller', 20, 60, 180, 400],
  ['lfo', 216, 60, 100, 400],
  ['dco', 332, 60, 300, 400],
  ['hpf', 648, 60, 60, 400],
  ['vcf', 724, 60, 230, 400],
  ['vca', 970, 60, 90, 400],
  ['env', 1076, 60, 160, 400],
  ['chorus', 1252, 60, 60, 400],
  ['memory', 20, 476, 1292, 76],
  ['right', 1328, 60, 52, 492],
];

const range = (pre: string, n: number) => Array.from({ length: n }, (_, i) => `${pre}${i + 1}`);

/** Every control in visual order: §5.2 sections, each in the order the table lists its controls. */
const ORDER: Record<'60' | '106', string[]> = {
  '60': [
    'volume', 'oct-transpose', 'lfo-trig', 'bend-dco', 'bend-vcf', 'lever',
    'lfo-rate', 'lfo-delay', 'lfo-mode',
    'dco-lfo', 'dco-pwm', 'pwm-mode', 'pulse', 'saw', 'sub', 'sub-level', 'noise', // no RANGE in 60 mode (C-2)
    'hpf',
    'vcf-freq', 'vcf-res', 'vcf-env', 'vcf-lfo', 'vcf-kybd', 'vcf-pol',
    'vca-mode', 'vca-level',
    'env-a', 'env-d', 'env-s', 'env-r',
    'chorus-i', 'chorus-ii',
    ...range('bank-', 5), ...range('patch-', 8), 'manual', 'write', 'hold', 'arp-on', 'arp-mode', 'arp-range', 'arp-rate',
    'tune',
  ],
  '106': [
    'volume', 'bend-dco', 'bend-vcf', 'lfo-trig-sens', 'porta-time', 'lever',
    'lfo-rate', 'lfo-delay',
    'dco-lfo', 'dco-pwm', 'pwm-mode', 'range-16', 'range-8', 'range-4', 'pulse', 'saw', 'sub-level', 'noise',
    'hpf',
    'vcf-freq', 'vcf-res', 'vcf-env', 'vcf-lfo', 'vcf-kybd', 'vcf-pol',
    'vca-mode', 'vca-level',
    'env-a', 'env-d', 'env-s', 'env-r',
    'chorus-off', 'chorus-i', 'chorus-ii',
    'group-a', 'group-b', ...range('bank-', 8), ...range('patch-', 8), 'manual', 'write', 'poly-1', 'poly-2',
    'portamento', 'key-transpose',
    'tune',
  ],
};

interface Focused {
  id: string;
  section: string;
  ring: { style: string; width: string; color: string; visible: boolean };
}

async function focusedInfo(page: Page): Promise<Focused | null> {
  return page.evaluate((boxes) => {
    const el = document.activeElement as HTMLElement | null;
    const tid = el?.getAttribute('data-testid') ?? '';
    if (!el || !tid.startsWith('ctl-')) return null;
    const stage = document.querySelector('[data-testid="stage"]')!;
    const scale = Number(stage.getAttribute('data-scale'));
    const sr = stage.getBoundingClientRect();
    const r = el.getBoundingClientRect();
    const x = (r.left + r.width / 2 - sr.left) / scale;
    const y = (r.top + r.height / 2 - sr.top) / scale;
    const box = boxes.find(([, bx, by, bw, bh]) => x >= bx && x <= bx + bw && y >= by && y <= by + bh);
    const cs = getComputedStyle(el);
    return {
      id: tid.slice(4),
      section: box ? box[0] : 'none',
      ring: { style: cs.outlineStyle, width: cs.outlineWidth, color: cs.outlineColor, visible: el.matches(':focus-visible') },
    };
  }, BOXES);
}

/** Every focusable panel control (switch position labels are not focusable). */
async function focusableControls(page: Page): Promise<string[]> {
  return page
    .locator('[data-testid^="ctl-"]')
    .evaluateAll((els) =>
      els
        .filter((e) => (e as HTMLElement).tabIndex >= 0 && !e.getAttribute('data-testid')!.includes('-pos-'))
        .map((e) => e.getAttribute('data-testid')!.slice(4)),
    );
}

const NAME_RULES = [
  'aria-command-name',
  'aria-dialog-name',
  'aria-input-field-name',
  'aria-meter-name',
  'aria-progressbar-name',
  'aria-toggle-field-name',
  'aria-tooltip-name',
  'button-name',
  'document-title',
  'image-alt',
  'input-button-name',
  'label',
  'link-name',
  'role-img-alt',
  'select-name',
  'svg-img-alt',
];

async function axeNames(page: Page, what: string): Promise<void> {
  const r = await new AxeBuilder({ page }).withRules(NAME_RULES).analyze();
  const missing = r.violations.flatMap((v) => v.nodes.map((n) => `${v.id}: ${n.target.join(' ')}`));
  expect(missing, `axe-core: no missing names (${what})`).toEqual([]);
}

for (const mode of ['60', '106'] as const) {
  test(`AC-U3.1 ${mode} skin: Tab reaches every control once in R-U3 order with a 2 px ring; axe finds no missing names`, async ({
    page,
    browserName,
  }) => {
    await openAndStart(page);
    if (mode === '106') await setMode(page, '106');
    const expected = ORDER[mode];
    expect((await focusableControls(page)).sort()).toEqual([...expected].sort());

    // Safari moves Tab only between text fields unless "Press Tab to highlight each item" is on;
    // Option+Tab is its key for "every control" (the same walk).
    const tab = browserName === 'webkit' ? 'Alt+Tab' : 'Tab';
    // Start the walk at the panel: a click on the left end cheek (no control there) sets the focus
    // navigation starting point. WebKit otherwise starts after the removed start card, at the end of
    // the document, where Tab first leaves the page.
    await page.getByTestId('panel').click({ position: { x: 5, y: 5 } });
    await page.evaluate(() => (document.activeElement as HTMLElement | null)?.blur());
    // the first Tab may stop on the document first (Chromium continues from the removed start card)
    let first: Focused | null = null;
    for (let k = 0; k < 3 && !first; k++) {
      await page.keyboard.press(tab);
      first = await focusedInfo(page);
    }
    expect(first?.id, 'Tab reaches the first control').toBe(expected[0]);
    const seen: Focused[] = [first!];
    for (let k = 1; k < expected.length; k++) {
      await page.keyboard.press(tab);
      const f = await focusedInfo(page);
      expect(f, `Tab ${k + 1} lands on a panel control`).not.toBeNull();
      seen.push(f!);
    }
    expect(seen.map((f) => f.id)).toEqual(expected);
    expect(new Set(seen.map((f) => f.id)).size, 'each control receives focus exactly once').toBe(expected.length);

    // visual order: section indices never go back (sections left to right, memory row, right edge)
    const sectionIndex = seen.map((f) => BOXES.findIndex(([id]) => id === f.section));
    expect(sectionIndex.every((i) => i >= 0), 'every control lies in its §5.2 box').toBe(true);
    for (let k = 1; k < sectionIndex.length; k++) {
      expect(sectionIndex[k], `${seen[k].id} after ${seen[k - 1].id}`).toBeGreaterThanOrEqual(sectionIndex[k - 1]);
    }
    // the visible focus ring: 2 px --legend (#ECE8DC)
    for (const f of seen) {
      expect(f.ring, `focus ring of ${f.id}`).toEqual({ style: 'solid', width: '2px', color: 'rgb(236, 232, 220)', visible: true });
    }
    // after the right edge: the top strip's BANKS and SETTINGS buttons (DECISIONS.md), then Tab leaves the panel
    for (const nav of ['nav-bank', 'nav-settings']) {
      await page.keyboard.press(tab);
      await expect(page.getByTestId(nav), `Tab after the right edge reaches ${nav}`).toBeFocused();
      await expect(page.getByTestId(nav)).toHaveCSS('outline-width', '2px');
    }
    await page.keyboard.press(tab);
    const next = await focusedInfo(page);
    expect(next?.id ?? null, 'no panel control after the top strip').toBeNull();
    await expect(page.getByTestId('nav-settings')).not.toBeFocused();

    // ARIA names equal the legends (spot checks) and axe-core finds no missing names
    await expect(ctl(page, 'vcf-freq')).toHaveAccessibleName('VCF FREQ');
    await expect(ctl(page, 'patch-3')).toHaveAccessibleName('PATCH 3');
    await expect(ctl(page, 'tune')).toHaveAccessibleName('TUNE');
    await axeNames(page, `${mode} panel`);
    await openBankManager(page);
    await axeNames(page, `${mode} bank manager`);
    await closeOverlay(page);
    await openSettings(page);
    await axeNames(page, `${mode} settings`);
    await closeOverlay(page);
  });
}
