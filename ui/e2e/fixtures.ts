// Shared fixtures for the VJN6 end-to-end tests. Every page gets the Web MIDI test double (§10) before
// its scripts run, and is opened with `?test`, which exposes window.__vjn6Test (the latest core state
// and the AnalyserNode tap). Actions go through the real UI; the hook is only read.
import { expect, test as base, type Locator, type Page } from '@playwright/test';
import { installMidiDouble, MIDI_IN_ID, MIDI_OUT_ID, type MidiDoubleApi, type MidiMode } from './midi-double';

export { expect };

/** The parts of the `state` message (docs/BRIDGE.md) the tests read. */
export interface CoreState {
  seq: number;
  mode: number;
  edit: number[];
  name: string;
  stored: number[];
  edited: boolean;
  manual: boolean;
  slot: number;
  pendingBank: number;
  pendingGroup: number;
  perf: number[];
  settings: number[];
  led: { text: string; dp: boolean; blink: boolean; message: number };
  lit: number[];
  blinking: number[];
  writeArmed: boolean;
  ktArmed: boolean;
  lfoTrig: boolean;
  push: boolean;
  lever: number;
}

export interface BankEntry {
  n: string;
  p: number[];
}

interface TestHook {
  state(): CoreState | null;
  bank(): BankEntry[];
  env(): { midiAvailable: boolean; sysexPermitted: boolean; midiOutAvailable: boolean };
  started(): boolean;
  rmsDb(): number | null;
  keyboard(): { base: number; velocity: number; play: boolean };
}

declare global {
  interface Window {
    __vjn6Test?: TestHook;
    __midiDouble?: MidiDoubleApi;
  }
}

/** Patch field indices (§4.1: B0–B15 = 0–15, W0–W8 = 16–24). */
export const F = {
  LFO_RATE: 0,
  LFO_DELAY: 1,
  DCO_LFO: 2,
  DCO_PWM: 3,
  NOISE: 4,
  VCF_FREQ: 5,
  VCF_RES: 6,
  VCF_ENV: 7,
  VCF_LFO: 8,
  VCF_KYBD: 9,
  VCA_LEVEL: 10,
  ENV_A: 11,
  ENV_D: 12,
  ENV_S: 13,
  ENV_R: 14,
  SUB_LEVEL: 15,
  RANGE: 16,
  PULSE: 17,
  SAW: 18,
  SUB: 19,
  CHORUS: 20,
  PWM_SRC: 21,
  VCF_POL: 22,
  VCA_MODE: 23,
  HPF: 24,
} as const;

/** Performance settings (§4.2, the protocol's Perf ids). */
export const P = {
  KEY_MODE: 0,
  PORTA_ON: 1,
  PORTA_TIME: 2,
  BEND_DCO: 3,
  BEND_VCF: 4,
  LFO_TRIG_SENS: 5,
  LFO_TRIG_MODE: 6,
  HOLD: 7,
  ARP_ON: 8,
  ARP_MODE: 9,
  ARP_RANGE: 10,
  ARP_RATE: 11,
  OCT_TRANSPOSE: 12,
  KEY_TRANSPOSE: 13,
  TUNE: 14,
  VOLUME: 15,
  MIDI_CHANNEL: 16,
  OMNI: 17,
  MIDI_FUNCTION: 18,
  PROTECT: 19,
} as const;

/** §4.4 settings held in the core state. */
export const S = { MODE: 0, CHORUS_NOISE: 1, VARIATION: 2, VELOCITY: 3, MONO: 4, OVERSAMPLE: 5, KBD_PLAY: 6, ZOOM: 7 } as const;

/** "Silent" for the analyser checks: −60 dBFS is AC-B2's sounding threshold. */
export const SOUND_DB = -60;

export const test = base.extend<{ midiMode: MidiMode }>({
  midiMode: ['grant', { option: true }],
  page: async ({ page, midiMode }, use) => {
    await page.addInitScript(installMidiDouble, { mode: midiMode, inId: MIDI_IN_ID, outId: MIDI_OUT_ID });
    await use(page);
  },
});

/** Opens the page in test mode and waits for the first complete state (R-S3). */
export async function openApp(page: Page, url = '/?test'): Promise<void> {
  await page.goto(url);
  await waitForApp(page);
}

/** Waits until the loaded page shows its first complete state (after goto or reload). */
export async function waitForApp(page: Page): Promise<void> {
  await page.waitForFunction(() => !!window.__vjn6Test?.state());
  await page.evaluate(() => document.fonts.ready.then(() => undefined));
}

/**
 * Dismisses the start card with a click on it (§5.7) and waits for the panel to light. With `audio`,
 * also waits for the AnalyserNode tap on the running worklet.
 */
export async function startApp(page: Page, opts: { audio?: boolean } = {}): Promise<void> {
  await expect(page.getByTestId('start-card')).toBeVisible();
  // a corner of the card that covers the wood cheek: no control lies under it
  await page.getByTestId('start-card').click({ position: { x: 4, y: 4 } });
  await expect(page.getByTestId('start-card')).toHaveCount(0);
  await page.waitForFunction(() => window.__vjn6Test?.started() === true);
  if (opts.audio) await waitForAudio(page);
  await page.mouse.move(1, 1);
}

export async function waitForAudio(page: Page): Promise<void> {
  await page.waitForFunction(() => typeof window.__vjn6Test?.rmsDb() === 'number', undefined, { timeout: 20_000 });
}

export async function openAndStart(page: Page, opts: { audio?: boolean } = {}): Promise<void> {
  await openApp(page);
  await startApp(page, opts);
}

export function state(page: Page): Promise<CoreState> {
  return page.evaluate(() => window.__vjn6Test!.state()!);
}

export function bank(page: Page): Promise<BankEntry[]> {
  return page.evaluate(() => window.__vjn6Test!.bank());
}

export function rmsDb(page: Page): Promise<number> {
  return page.evaluate(() => window.__vjn6Test!.rmsDb() ?? -Infinity);
}

/** Waits until the core state satisfies `pred` (the UI receives changes within 100 ms, R-S3). */
export async function waitForState(page: Page, pred: (s: CoreState) => boolean, message?: string): Promise<CoreState> {
  await expect.poll(async () => pred(await state(page)), { message, timeout: 5_000, intervals: [20, 50, 100] }).toBe(true);
  return state(page);
}

/** Highest RMS seen over `ms` (polled every animation frame in the page). */
export function peakRmsOver(page: Page, ms: number): Promise<number> {
  return page.evaluate(
    (dur) =>
      new Promise<number>((resolve) => {
        let peak = -Infinity;
        const t0 = performance.now();
        const tick = () => {
          const v = window.__vjn6Test!.rmsDb() ?? -Infinity;
          if (v > peak) peak = v;
          if (performance.now() - t0 < dur) requestAnimationFrame(tick);
          else resolve(peak);
        };
        tick();
      }),
    ms,
  );
}

/** Waits until the analyser RMS rises above (or falls below) `db`. */
export async function waitForRms(page: Page, cmp: 'above' | 'below', db: number, timeout = 5_000): Promise<number> {
  await expect
    .poll(async () => {
      const v = await rmsDb(page);
      return cmp === 'above' ? v > db : v < db;
    }, { timeout, intervals: [25, 50, 100] })
    .toBe(true);
  return rmsDb(page);
}

export function ctl(page: Page, id: string): Locator {
  return page.getByTestId(`ctl-${id}`);
}

export function midi(page: Page) {
  return {
    inject: (bytes: number[]) => page.evaluate((b) => window.__midiDouble!.inject(b), bytes),
    sent: () => page.evaluate(() => window.__midiDouble!.sent()),
    requests: () => page.evaluate(() => window.__midiDouble!.requests()),
    waitListening: () => page.waitForFunction(() => window.__midiDouble?.listening() === true),
  };
}

/** Current Fit/zoom scale of the canvas (logical px → CSS px). */
export async function canvasScale(page: Page): Promise<number> {
  return Number(await page.getByTestId('stage').getAttribute('data-scale'));
}

/** Opens Settings with its shortcut (R-U2: Alt+,). */
export async function openSettings(page: Page): Promise<Locator> {
  await page.keyboard.press('Alt+Comma');
  const dlg = page.getByTestId('settings');
  await expect(dlg).toBeVisible();
  return dlg;
}

/** Opens the bank manager with its shortcut (R-U2: Alt+B). */
export async function openBankManager(page: Page): Promise<Locator> {
  await page.keyboard.press('Alt+KeyB');
  const dlg = page.getByTestId('bank-manager');
  await expect(dlg).toBeVisible();
  return dlg;
}

export async function closeOverlay(page: Page): Promise<void> {
  await page.keyboard.press('Escape');
  await expect(page.getByTestId('settings')).toHaveCount(0);
  await expect(page.getByTestId('bank-manager')).toHaveCount(0);
}

/**
 * Chorus noise OFF (§4.4) through the Settings overlay. "No note sounds" means RMS below SOUND_DB with chorus
 * noise off: the hiss of a patch with chorus (slot 1, `Strings 1`, has chorus I) peaks at about −60 dBFS.
 */
export async function setChorusNoiseOff(page: Page): Promise<void> {
  const dlg = await openSettings(page);
  await dlg.getByTestId('set-chorus-noise-0').click();
  await waitForState(page, (s) => s.settings[S.CHORUS_NOISE] === 0);
  await closeOverlay(page);
}

/** Switches the engine mode through the Settings overlay (§5.6, F-12) and closes it. */
export async function setMode(page: Page, mode: '60' | '106'): Promise<void> {
  await openSettings(page);
  await page.getByTestId(`set-mode-${mode}`).click();
  await waitForState(page, (s) => s.mode === (mode === '106' ? 1 : 0));
  await closeOverlay(page);
  await expect(page.getByTestId('panel')).toHaveAttribute('data-mode', mode);
}

/** Memory protect through the Settings overlay (F-6). */
export async function setProtect(page: Page, on: boolean): Promise<void> {
  await openSettings(page);
  await page.getByTestId(`set-protect-${on ? 1 : 0}`).click();
  await waitForState(page, (s) => s.perf[P.PROTECT] === (on ? 1 : 0));
  await closeOverlay(page);
}

/** Centre of an element in CSS px. */
export async function centre(loc: Locator): Promise<{ x: number; y: number }> {
  const b = await loc.boundingBox();
  if (!b) throw new Error('element has no box');
  return { x: b.x + b.width / 2, y: b.y + b.height / 2 };
}
