// Front panel: AC-P1.1 (UI part: control map per mode, no RANGE in 60 mode, the 60 VCF order, the 60
// chorus radio buttons), AC-U4.1 (combination presses), AC-P5.1 (UI part: the centre-zero 60 VCA
// LEVEL) and AC-S3.3 (a mocked MIDI CC is visible within 100 ms).
import type { Page } from '@playwright/test';
import { ctl, expect, F, midi, openAndStart, P, setMode, state, test, waitForState, type CoreState } from './fixtures';

type Target = { field: number } | { perf: number } | { lever: true } | { button: true };

/** §5.2 controls per mode with their §4.1 / §4.2 binding (R-P1); ids are the panel's data-testids. */
const SLIDERS: Record<string, number> = {
  'lfo-rate': F.LFO_RATE,
  'lfo-delay': F.LFO_DELAY,
  'dco-lfo': F.DCO_LFO,
  'dco-pwm': F.DCO_PWM,
  'sub-level': F.SUB_LEVEL,
  noise: F.NOISE,
  'vcf-freq': F.VCF_FREQ,
  'vcf-res': F.VCF_RES,
  'vcf-env': F.VCF_ENV,
  'vcf-lfo': F.VCF_LFO,
  'vcf-kybd': F.VCF_KYBD,
  'vca-level': F.VCA_LEVEL,
  'env-a': F.ENV_A,
  'env-d': F.ENV_D,
  'env-s': F.ENV_S,
  'env-r': F.ENV_R,
};

const BOTH: Record<string, Target> = {
  volume: { perf: P.VOLUME },
  'bend-dco': { perf: P.BEND_DCO },
  'bend-vcf': { perf: P.BEND_VCF },
  lever: { lever: true },
  ...Object.fromEntries(Object.entries(SLIDERS).map(([id, field]) => [id, { field }])),
  'pwm-mode': { field: F.PWM_SRC },
  pulse: { field: F.PULSE },
  saw: { field: F.SAW },
  hpf: { field: F.HPF },
  'vcf-pol': { field: F.VCF_POL },
  'vca-mode': { field: F.VCA_MODE },
  'chorus-off': { field: F.CHORUS },
  'chorus-i': { field: F.CHORUS },
  'chorus-ii': { field: F.CHORUS },
  ...Object.fromEntries([1, 2, 3, 4, 5].map((i) => [`bank-${i}`, { button: true }])),
  ...Object.fromEntries([1, 2, 3, 4, 5, 6, 7, 8].map((i) => [`patch-${i}`, { button: true }])),
  manual: { button: true },
  write: { button: true },
  tune: { perf: P.TUNE },
};

const ONLY_60: Record<string, Target> = {
  'oct-transpose': { perf: P.OCT_TRANSPOSE },
  'lfo-trig': { button: true },
  'lfo-mode': { perf: P.LFO_TRIG_MODE },
  sub: { field: F.SUB },
  hold: { perf: P.HOLD },
  'arp-on': { perf: P.ARP_ON },
  'arp-mode': { perf: P.ARP_MODE },
  'arp-range': { perf: P.ARP_RANGE },
  'arp-rate': { perf: P.ARP_RATE },
};

/** RANGE is 106-only: the Juno-60 has no DCO RANGE control (C-2). */
const ONLY_106: Record<string, Target> = {
  'lfo-trig-sens': { perf: P.LFO_TRIG_SENS },
  'porta-time': { perf: P.PORTA_TIME },
  'range-16': { field: F.RANGE },
  'range-8': { field: F.RANGE },
  'range-4': { field: F.RANGE },
  'group-a': { button: true },
  'group-b': { button: true },
  'bank-6': { button: true },
  'bank-7': { button: true },
  'bank-8': { button: true },
  'poly-1': { perf: P.KEY_MODE },
  'poly-2': { perf: P.KEY_MODE },
  portamento: { perf: P.PORTA_ON },
  'key-transpose': { button: true },
};

/** The ids of every control drawn on the panel (position labels of switches excluded). */
async function controlIds(page: Page): Promise<string[]> {
  const ids = await page
    .locator('[data-testid^="ctl-"]')
    .evaluateAll((els) => els.map((e) => e.getAttribute('data-testid')!.slice(4)).filter((id) => !id.includes('-pos-')));
  return ids.sort();
}

function read(s: CoreState, t: Target): number {
  if ('field' in t) return s.edit[t.field];
  if ('perf' in t) return s.perf[t.perf];
  if ('lever' in t) return s.lever;
  return NaN;
}

/** Moves a slider, knob, switch or lever with the keyboard (R-U1a) and checks its bound value moved. */
async function moveAndCheck(page: Page, id: string, t: Target): Promise<void> {
  const loc = ctl(page, id);
  const role = await loc.getAttribute('role');
  if ('button' in t || role !== 'slider') return; // buttons are checked by the dedicated tests
  const before = read(await state(page), t);
  await loc.focus();
  if ('lever' in t) {
    await page.keyboard.down('ArrowRight');
    await waitForState(page, (s) => s.lever === 8191, `${id}: lever fully right while held`);
    await page.keyboard.up('ArrowRight');
    await waitForState(page, (s) => s.lever === 0, `${id}: lever back to centre`);
    return;
  }
  const isSwitch = (await loc.getAttribute('data-field')) !== null;
  if (isSwitch) {
    // Up moves towards the top position; from the top, Down (R-U1a)
    const atTop = await page.locator(`[data-testid^="ctl-${id}-pos-"]`).first().evaluate((e) => e.classList.contains('active'));
    await page.keyboard.press(atTop ? 'ArrowDown' : 'ArrowUp');
    await expect.poll(async () => read(await state(page), t), { message: `${id} moves its field` }).not.toBe(before);
    await expect(loc).toHaveAttribute('data-value', String(read(await state(page), t)));
    return;
  }
  const max = Number(await loc.getAttribute('aria-valuemax'));
  const key = before === max ? 'Home' : 'End';
  await page.keyboard.press(key);
  const want = key === 'Home' ? 0 : max;
  await waitForState(page, (s) => read(s, t) === want, `${id} → ${want}`);
  await expect(loc).toHaveAttribute('data-value', String(want));
}

const TOGGLES: Record<string, Target> = {
  pulse: { field: F.PULSE },
  saw: { field: F.SAW },
  sub: { field: F.SUB },
  hold: { perf: P.HOLD },
  'arp-on': { perf: P.ARP_ON },
  portamento: { perf: P.PORTA_ON },
};

/** Clicks every button of the mode and checks the field, setting or panel state it is bound to (§5.2). */
async function pressButtons(page: Page, mode: '60' | '106'): Promise<void> {
  for (const [id, t] of Object.entries(TOGGLES)) {
    if (!(await ctl(page, id).count())) continue;
    const before = read(await state(page), t);
    await ctl(page, id).click();
    await waitForState(page, (s) => read(s, t) === 1 - before, `${id} toggles`);
    await expect(ctl(page, id)).toHaveAttribute('data-lit', String(before === 0));
  }
  if (mode === '106') {
    for (const [i, id] of ['range-16', 'range-8', 'range-4'].entries()) {
      await ctl(page, id).click();
      await waitForState(page, (s) => s.edit[F.RANGE] === i, `${id} → W0 = ${i}`);
      await expect(ctl(page, id)).toHaveAttribute('data-lit', 'true');
    }
    for (const [id, w4] of [['chorus-ii', 2], ['chorus-i', 1], ['chorus-off', 0]] as const) {
      await ctl(page, id).click();
      await waitForState(page, (s) => s.edit[F.CHORUS] === w4, `${id} → W4 = ${w4}`);
    }
    for (const [id, km] of [['poly-2', 1], ['poly-1', 0]] as const) {
      await ctl(page, id).click();
      await waitForState(page, (s) => s.perf[P.KEY_MODE] === km, `${id} → key mode ${km}`);
    }
    await ctl(page, 'group-b').click();
    await waitForState(page, (s) => s.pendingGroup === 1, 'GROUP B');
    await ctl(page, 'group-a').click();
    await waitForState(page, (s) => s.pendingGroup === 0, 'GROUP A');
    await ctl(page, 'key-transpose').click();
    await waitForState(page, (s) => s.ktArmed, 'KEY TRANSPOSE arms');
    await expect(page.getByTestId('led')).toHaveAttribute('data-raw-text', 't-');
    await ctl(page, 'key-transpose').click();
    await waitForState(page, (s) => !s.ktArmed, 'KEY TRANSPOSE disarms');
  } else {
    const trig = ctl(page, 'lfo-trig');
    await trig.hover();
    await page.mouse.down();
    await waitForState(page, (s) => s.lfoTrig, 'LFO TRIG held');
    await expect(trig).toHaveAttribute('data-lit', 'true');
    await page.mouse.up();
    await waitForState(page, (s) => !s.lfoTrig, 'LFO TRIG released');
  }
  const banks = mode === '60' ? 5 : 8;
  for (let b = 1; b <= banks; b++) {
    await ctl(page, `bank-${b}`).click();
    await waitForState(page, (s) => s.pendingBank === b, `BANK ${b}`);
  }
  await ctl(page, 'bank-2').click();
  for (let p = 1; p <= 8; p++) {
    await ctl(page, `patch-${p}`).click();
    await waitForState(page, (s) => s.slot === 8 + p && !s.manual, `PATCH ${p} → slot ${8 + p}`);
    await expect(page.getByTestId('led')).toHaveAttribute('data-text', `2${p}`);
  }
  await ctl(page, 'manual').click();
  await waitForState(page, (s) => s.manual, 'MANUAL');
  await expect(page.getByTestId('led')).toHaveAttribute('data-text', '--');
  await ctl(page, 'write').click();
  await waitForState(page, (s) => s.writeArmed, 'WRITE arms');
  await ctl(page, 'write').click();
  await waitForState(page, (s) => !s.writeArmed, 'WRITE again cancels');
}

/** §5.2 VCF order per mode (C-5): the one box whose shared controls sit at different x in the two modes. */
const VCF_ORDER = {
  '60': ['vcf-freq', 'vcf-res', 'vcf-pol', 'vcf-env', 'vcf-lfo', 'vcf-kybd'],
  '106': ['vcf-freq', 'vcf-res', 'vcf-env', 'vcf-lfo', 'vcf-kybd', 'vcf-pol'],
};

/** The VCF controls left to right, in DOM order (= Tab order, R-U3), and their logical boxes. */
async function vcfLayout(page: Page): Promise<{ byX: string[]; dom: string[]; boxes: Record<string, number[]> }> {
  return page.evaluate(() => {
    const stage = document.querySelector('[data-testid="stage"]')!;
    const scale = Number(stage.getAttribute('data-scale'));
    const sr = stage.getBoundingClientRect();
    const els = [...document.querySelectorAll('[data-testid^="ctl-vcf-"]')].filter(
      (e) => !e.getAttribute('data-testid')!.includes('-pos-'),
    );
    const boxes: Record<string, number[]> = {};
    for (const e of els) {
      const r = e.getBoundingClientRect();
      boxes[e.getAttribute('data-testid')!.slice(4)] = [(r.left - sr.left) / scale, (r.right - sr.left) / scale];
    }
    const dom = Object.keys(boxes);
    return { dom, byX: [...dom].sort((a, b) => boxes[a][0] - boxes[b][0]), boxes };
  });
}

test.describe('AC-P1.1 control map (UI part)', () => {
  for (const mode of ['60', '106'] as const) {
    test(`AC-P1.1 ${mode} mode: every control is present only in its mode and moves its §4.1/§4.2 field or action`, async ({
      page,
    }) => {
      await openAndStart(page);
      if (mode === '106') await setMode(page, '106');
      const own = mode === '60' ? ONLY_60 : ONLY_106;
      const expected = { ...BOTH, ...own };
      expect(await controlIds(page)).toEqual(Object.keys(expected).sort());
      for (const id of Object.keys(mode === '60' ? ONLY_106 : ONLY_60)) {
        if (!(id in expected)) await expect(ctl(page, id), `${id} is not drawn in ${mode} mode`).toHaveCount(0);
      }
      // mode-specific forms of shared controls (§5.2)
      if (mode === '60') {
        await expect(ctl(page, 'hpf')).toHaveAttribute('data-field', 'field'); // 4-position switch
        await expect(ctl(page, 'hpf-pos-3')).toHaveCount(1);
        await expect(ctl(page, 'pwm-mode-pos-2')).toHaveText('ENV');
        await expect(ctl(page, 'vcf-pol-pos-1')).toHaveText('−');
        await expect(page.getByTestId('text-range'), 'no RANGE legend in the DCO box (C-2)').toHaveCount(0);
        // OCTAVE TRANSPOSE D/N/U (C-3), top to bottom U, N, D
        await expect(page.locator('[data-testid^="ctl-oct-transpose-pos-"]')).toHaveText(['U', 'N', 'D']);
        for (const [v, label] of [[2, 'U'], [1, 'N'], [0, 'D']] as const) {
          await expect(ctl(page, `oct-transpose-pos-${v}`)).toHaveText(label);
        }
      } else {
        await expect(page.getByTestId('text-range')).toHaveText('RANGE');
        await expect(ctl(page, 'hpf')).not.toHaveAttribute('data-field', /.*/); // 4-position slider
        await expect(ctl(page, 'hpf')).toHaveAttribute('aria-valuemax', '3');
        await expect(ctl(page, 'pwm-mode-pos-2')).toHaveCount(0);
        await expect(ctl(page, 'vcf-pol-pos-1')).toHaveText('INVERTED');
      }
      await expect(page.getByTestId('mode-badge')).toHaveText(mode);

      for (const [id, t] of Object.entries(expected)) await moveAndCheck(page, id, t);
      await pressButtons(page, mode);
    });
  }

  test('AC-P1.1 60 mode: no RANGE control, its place stays empty, PULSE and SAW do not move, a stored 16′ is kept for 106 mode (C-2, §8)', async ({
    page,
  }) => {
    await openAndStart(page);
    await setMode(page, '106');
    await ctl(page, 'range-16').click();
    await waitForState(page, (s) => s.edit[F.RANGE] === 0, 'RANGE 16′ → W0 = 0');
    await expect(ctl(page, 'range-16')).toHaveAttribute('data-lit', 'true');
    const boxes = async () => Promise.all(['pulse', 'saw'].map((id) => ctl(page, id).boundingBox()));
    const shared106 = await boxes();
    const rangeBoxes = await Promise.all(['range-16', 'range-8', 'range-4'].map((id) => ctl(page, id).boundingBox()));

    await setMode(page, '60');
    for (const id of ['range-16', 'range-8', 'range-4']) await expect(ctl(page, id)).toHaveCount(0);
    await expect(page.getByTestId('text-range')).toHaveCount(0);
    expect(await boxes(), 'PULSE and SAW keep their 106 positions').toEqual(shared106);
    // the RANGE place is empty panel: no control under the former button centres
    for (const b of rangeBoxes) {
      const hit = await page.evaluate(
        ({ x, y }) => document.elementFromPoint(x, y)?.closest('[data-testid^="ctl-"]')?.getAttribute('data-testid') ?? null,
        { x: b!.x + b!.width / 2, y: b!.y + b!.height / 2 },
      );
      expect(hit, 'nothing is drawn where RANGE sits in 106 mode').toBeNull();
    }
    const s60 = await state(page);
    expect(s60.edit[F.RANGE], 'the stored W0 is kept (lossless)').toBe(0);
    // ButtonLed bits L_RANGE16, L_RANGE8, L_RANGE4
    for (const bit of [0, 1, 2]) expect(s60.lit, 'no RANGE LED in 60 mode').not.toContain(bit);

    await setMode(page, '106');
    await expect(ctl(page, 'range-16')).toHaveAttribute('data-lit', 'true');
    expect((await state(page)).edit[F.RANGE]).toBe(0);
  });

  test('AC-P1.1 VCF order per mode (C-5): 60 FREQ, RES, polarity, ENV, LFO, KYBD; 106 FREQ, RES, ENV, LFO, KYBD, polarity; also after mode switches', async ({
    page,
  }) => {
    await openAndStart(page);
    const seen: Record<string, Record<string, number[]>> = {};
    for (const [k, mode] of (['60', '106', '60'] as const).entries()) {
      if (k > 0) await setMode(page, mode);
      const l = await vcfLayout(page);
      expect(l.byX, `${mode} mode: left to right`).toEqual(VCF_ORDER[mode]);
      expect(l.dom, `${mode} mode: DOM (Tab) order`).toEqual(VCF_ORDER[mode]);
      for (const [id, [x0, x1]] of Object.entries(l.boxes)) {
        expect(x0 >= 724 && x1 <= 954, `${mode} mode: ${id} inside the VCF box (${x0}–${x1})`).toBe(true);
      }
      if (seen[mode]) expect(l.boxes, `${mode} mode: the same positions after switching back`).toEqual(seen[mode]);
      seen[mode] = l.boxes;
    }
    // FREQ and RES are shared and do not move
    for (const id of ['vcf-freq', 'vcf-res']) expect(seen['60'][id], id).toEqual(seen['106'][id]);
    // the moved switch still writes W6 (§4.1)
    await ctl(page, 'vcf-pol-pos-1').click();
    await waitForState(page, (s) => s.edit[F.VCF_POL] === 1, 'polarity − → W6 = 1');
    await expect(ctl(page, 'vcf-pol')).toHaveAttribute('aria-valuetext', '−');
  });

  test('AC-P1.1 60 mode: chorus OFF, I, II are radio buttons (OFF without LED); the CHORUS I ↔ II combination press gives I+II (C-6)', async ({
    page,
  }) => {
    await openAndStart(page);
    const btn = { off: ctl(page, 'chorus-off'), i: ctl(page, 'chorus-i'), ii: ctl(page, 'chorus-ii') };
    await expect(page.getByTestId('lamp-chorus-off'), 'the 60 OFF button has no LED').toHaveCount(0);
    await expect(btn.off, 'a plain push button: no pressed state').not.toHaveAttribute('aria-pressed', /.*/);
    // [button, Shift held, W4 after, I lit, II lit]
    const steps: [keyof typeof btn, boolean, number, boolean, boolean][] = [
      ['off', false, 0, false, false], // → off (from any start)
      ['i', false, 1, true, false], // off → I
      ['ii', false, 2, false, true], // I → II (radio)
      ['i', false, 1, true, false], // II → I
      ['off', false, 0, false, false], // I → off
      ['i', true, 1, true, false], // Shift without a lit partner = plain click
      ['ii', true, 3, true, true], // Shift+II with I lit → I+II
      ['ii', false, 2, false, true], // a plain click in I+II selects that single mode
      ['i', true, 3, true, true], // Shift+I with II lit → I+II
      ['i', false, 1, true, false],
      ['ii', true, 3, true, true],
      ['off', false, 0, false, false], // I+II → off
    ];
    for (const [id, shift, w4, litI, litII] of steps) {
      await btn[id].click({ modifiers: shift ? ['Shift'] : [] });
      await waitForState(page, (s) => s.edit[F.CHORUS] === w4, `${shift ? 'Shift+' : ''}${id} → W4 = ${w4}`);
      await expect(btn.i).toHaveAttribute('data-lit', String(litI));
      await expect(btn.ii).toHaveAttribute('data-lit', String(litII));
      await expect(btn.off).toHaveAttribute('data-lit', 'false');
      expect((await state(page)).lit, 'L_CHORUS_OFF (bit 6) is never lit in 60 mode').not.toContain(6);
    }
    // a click on the selected radio button changes nothing
    const seq = (await state(page)).seq;
    await btn.off.click();
    await page.waitForTimeout(250);
    expect((await state(page)).seq).toBe(seq);
    // the I and II tooltips name the Shift+click in 60 mode only (R-U4)
    const tip = page.getByTestId('tooltip');
    for (const [id, text] of [
      ['off', 'CHORUS OFF'],
      ['i', 'CHORUS I — Shift+click with CHORUS II lit: I+II'],
      ['ii', 'CHORUS II — Shift+click with CHORUS I lit: I+II'],
    ] as const) {
      await btn[id].hover();
      await expect(tip).toHaveText(text);
    }

    await setMode(page, '106');
    await expect(page.getByTestId('lamp-chorus-off')).toHaveCount(1);
    await expect(btn.off).toHaveAttribute('aria-pressed', 'true');
    for (const [id, text] of [
      ['i', 'CHORUS I'],
      ['ii', 'CHORUS II'],
    ] as const) {
      await btn[id].hover();
      await expect(tip).toHaveText(text);
    }
    // no combination in 106 mode: Shift+click I with II lit is a plain click
    await btn.ii.click();
    await waitForState(page, (s) => s.edit[F.CHORUS] === 2, '106 II → W4 = 2');
    await btn.i.click({ modifiers: ['Shift'] });
    await waitForState(page, (s) => s.edit[F.CHORUS] === 1, '106 Shift+I → W4 = 1');
    await expect(btn.i).toHaveAttribute('data-lit', 'true');
    await expect(btn.ii).toHaveAttribute('data-lit', 'false');
  });
});

test('AC-U4.1 60 mode, CHORUS II lit: Shift+click I → I+II; Shift+click I → unchanged; click I → I (C-6)', async ({
  page,
}) => {
  await openAndStart(page);
  const i = ctl(page, 'chorus-i');
  const ii = ctl(page, 'chorus-ii');
  await ii.click();
  await waitForState(page, (s) => s.edit[F.CHORUS] === 2, 'II');
  await expect(i).toHaveAttribute('data-lit', 'false');
  await expect(ii).toHaveAttribute('data-lit', 'true');

  await i.click({ modifiers: ['Shift'] });
  await waitForState(page, (s) => s.edit[F.CHORUS] === 3, 'I+II');
  await expect(i).toHaveAttribute('data-lit', 'true');
  await expect(ii).toHaveAttribute('data-lit', 'true');

  const seq = (await state(page)).seq;
  await i.click({ modifiers: ['Shift'] });
  await page.waitForTimeout(250);
  const s = await state(page);
  expect(s.edit[F.CHORUS], 'repeating the active combination does nothing').toBe(3);
  expect(s.seq).toBe(seq);
  await expect(i).toHaveAttribute('data-lit', 'true');
  await expect(ii).toHaveAttribute('data-lit', 'true');

  await i.click();
  await waitForState(page, (st) => st.edit[F.CHORUS] === 1, 'I');
  await expect(i).toHaveAttribute('data-lit', 'true');
  await expect(ii).toHaveAttribute('data-lit', 'false');
  // Shift+Enter on the focused button is the same combination press (R-U4)
  await ii.focus();
  await page.keyboard.press('Shift+Enter');
  await waitForState(page, (st) => st.edit[F.CHORUS] === 3, 'Shift+Enter on II with I lit → I+II');
});

test('AC-U4.1 106 mode, POLY 1 lit: Shift+click POLY 2 → UNISON; Shift+click POLY 1 → unchanged; click POLY 1 → POLY 1', async ({
  page,
}) => {
  await openAndStart(page);
  await setMode(page, '106');
  const p1 = ctl(page, 'poly-1');
  const p2 = ctl(page, 'poly-2');
  expect((await state(page)).perf[P.KEY_MODE]).toBe(0);
  await expect(p1).toHaveAttribute('data-lit', 'true');
  await expect(p2).toHaveAttribute('data-lit', 'false');

  await p2.click({ modifiers: ['Shift'] });
  await waitForState(page, (s) => s.perf[P.KEY_MODE] === 2, 'UNISON');
  await expect(p1).toHaveAttribute('data-lit', 'true');
  await expect(p2).toHaveAttribute('data-lit', 'true');

  const seq = (await state(page)).seq;
  await p1.click({ modifiers: ['Shift'] });
  await page.waitForTimeout(250);
  const s = await state(page);
  expect(s.perf[P.KEY_MODE], 'repeating the active combination does nothing').toBe(2);
  expect(s.seq).toBe(seq);
  await expect(p1).toHaveAttribute('data-lit', 'true');
  await expect(p2).toHaveAttribute('data-lit', 'true');

  await p1.click();
  await waitForState(page, (st) => st.perf[P.KEY_MODE] === 0, 'POLY 1');
  await expect(p1).toHaveAttribute('data-lit', 'true');
  await expect(p2).toHaveAttribute('data-lit', 'false');
  // the tooltip names the Shift+click (R-U4)
  await p2.hover();
  await expect(page.getByTestId('tooltip')).toHaveText('POLY 2 — Shift+click with POLY 1 lit: UNISON');
});

/** Sets VCA LEVEL to `byte` with the keyboard (R-U1a) and reads its v on the panel, in the tooltip and in typed entry. */
async function vcaLevelShown(page: Page, byte: number): Promise<{ panel: string; tooltip: string; typed: string }> {
  const loc = ctl(page, 'vca-level');
  await loc.focus();
  await page.keyboard.press(byte > 64 ? 'End' : 'Home');
  for (let b = byte > 64 ? 127 : 0; b !== byte; ) {
    const step = Math.abs(byte - b) >= 10 ? 10 : 1;
    await page.keyboard.press(`${step === 10 ? 'Shift+' : ''}${byte > b ? 'ArrowUp' : 'ArrowDown'}`);
    b += byte > b ? step : -step;
  }
  await waitForState(page, (s) => s.edit[F.VCA_LEVEL] === byte, `VCA LEVEL byte ${byte}`);
  await expect(loc).toHaveAttribute('data-value', String(byte));
  await page.mouse.move(1, 1);
  await loc.hover();
  const tip = page.getByTestId('tooltip');
  await expect(tip).toBeVisible();
  const tooltip = (await tip.textContent()) ?? '';
  await loc.dblclick();
  const typed = await page.getByTestId('typed-entry').inputValue();
  await page.getByTestId('typed-entry').press('Escape');
  await expect(page.getByTestId('typed-entry')).toHaveCount(0);
  expect((await state(page)).edit[F.VCA_LEVEL], 'reading never changes the value').toBe(byte);
  return { panel: (await loc.getAttribute('aria-valuetext')) ?? '', tooltip, typed };
}

test('AC-P5.1 VCA LEVEL at bytes 0, 64, 127: 60 mode is centre-zero (-5.0, 0.0, +5.0 on a −5…+5 scale), 106 mode unchanged (C-4)', async ({
  page,
}) => {
  await openAndStart(page);
  const ticks = (id: string) => page.getByTestId(`scale-${id}`).locator('text');
  const signed = ['−5', '−4', '−3', '−2', '−1', '0', '+1', '+2', '+3', '+4', '+5']; // bottom to top
  const plain = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '10'];
  await expect(ticks('vca-level')).toHaveText(signed);
  await expect(ticks('env-a'), 'every other slider keeps 0–10').toHaveText(plain);
  for (const [byte, v, db] of [
    [0, '-5.0', '-inf dB'],
    [64, '0.0', '-6.0 dB'],
    [127, '+5.0', '+0.0 dB'],
  ] as const) {
    const want = `VCA LEVEL ${v} = ${db}`;
    expect(await vcaLevelShown(page, byte), `60 mode, byte ${byte}`).toEqual({ panel: want, tooltip: want, typed: v });
  }

  await setMode(page, '106');
  await expect(ticks('vca-level')).toHaveText(plain);
  for (const [byte, v, db] of [
    [0, '0.0', '-inf dB'],
    [64, '5.0', '-6.0 dB'],
    [127, '10.0', '+0.0 dB'],
  ] as const) {
    const want = `VCA LEVEL ${v} = ${db}`;
    expect(await vcaLevelShown(page, byte), `106 mode, byte ${byte}`).toEqual({ panel: want, tooltip: want, typed: v });
  }
});

test('AC-S3.3 web with mocked MIDI: CC74 64 injected → the FREQ slider shows 5.0 within 100 ms', async ({ page }) => {
  await openAndStart(page, { audio: true });
  const m = midi(page);
  await m.waitListening();
  const freq = ctl(page, 'vcf-freq');
  const start = (await state(page)).edit[F.VCF_FREQ]; // slot 1, `Strings 1`
  expect(start).not.toBe(64);
  await expect(freq).toHaveAttribute('data-value', String(start));

  // measured in the page: from the injected message to the slider's displayed value
  const measure = (cc: number, shown: string) =>
    page.evaluate(
      ({ cc, shown }) =>
        new Promise<number>((resolve) => {
          const el = document.querySelector('[data-testid="ctl-vcf-freq"]')!;
          const ok = () => (el.getAttribute('aria-valuetext') ?? '').startsWith(`VCF FREQ ${shown} = `);
          const t0 = performance.now();
          const obs = new MutationObserver(() => {
            if (ok()) {
              obs.disconnect();
              resolve(performance.now() - t0);
            }
          });
          obs.observe(el, { attributes: true });
          window.__midiDouble!.inject([0xb0, 74, cc]);
          setTimeout(() => {
            obs.disconnect();
            resolve(Infinity);
          }, 2000);
        }),
      { cc, shown },
    );

  const times: number[] = [];
  for (let k = 0; k < 5; k++) {
    times.push(await measure(64, '5.0'));
    await expect(freq).toHaveAttribute('data-value', '64');
    await measure(127, '10.0');
  }
  test.info().annotations.push({ type: 'latency', description: times.map((t) => `${t.toFixed(1)} ms`).join(', ') });
  for (const t of times) expect(t, 'CC74 → FREQ shows 5.0').toBeLessThanOrEqual(100);
  await m.inject([0xb0, 74, 64]);
  await expect(freq).toHaveAttribute('data-value', '64');
  await expect(freq).toHaveAttribute('aria-valuetext', /^VCF FREQ 5\.0 = /);
  await freq.hover();
  await expect(page.getByTestId('tooltip')).toContainText('VCF FREQ 5.0 = ');
  const s = await state(page);
  expect(s.edit[F.VCF_FREQ]).toBe(64);
  expect(s.edited).toBe(true); // an external change sets EDITED (R-S2) …
  expect(s.slot).toBe(1); // … and never changes the slot
});

test('Top strip: SETTINGS and BANKS open their overlays by click; the mode is switched 60 → 106 → 60 with the mouse only', async ({
  page,
}) => {
  await openAndStart(page);
  const panel = page.getByTestId('panel');
  await expect(panel).toHaveAttribute('data-mode', '60');
  for (const mode of ['106', '60'] as const) {
    await page.getByTestId('nav-settings').click();
    const dlg = page.getByTestId('settings');
    await expect(dlg).toBeVisible();
    await dlg.getByTestId(`set-mode-${mode}`).click();
    await waitForState(page, (s: CoreState) => s.mode === (mode === '106' ? 1 : 0));
    await page.getByTestId('overlay-backdrop').click({ position: { x: 5, y: 5 } });
    await expect(dlg).toHaveCount(0);
    await expect(panel).toHaveAttribute('data-mode', mode);
    await expect(page.getByTestId('mode-badge')).toHaveText(mode);
    // drawn in both modes
    await expect(page.getByTestId('nav-settings')).toBeVisible();
    await expect(page.getByTestId('nav-bank')).toBeVisible();
  }
  await page.getByTestId('nav-bank').click();
  await expect(page.getByTestId('bank-manager')).toBeVisible();
  await page.keyboard.press('Escape');
  await expect(page.getByTestId('bank-manager')).toHaveCount(0);
  await expect(page.getByTestId('nav-settings')).toHaveAccessibleName('SETTINGS');
  await expect(page.getByTestId('nav-bank')).toHaveAccessibleName('BANKS');
});
