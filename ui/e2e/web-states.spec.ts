// §5.7 web page states and §5.6 MIDI states: AC-SC2, and the web part of AC-F1.1.
import { NOISO_ORIGIN } from './env';
import {
  centre,
  ctl,
  expect,
  F,
  openApp,
  openBankManager,
  openSettings,
  P,
  peakRmsOver,
  setMode,
  SOUND_DB,
  startApp,
  state,
  test,
  waitForAudio,
  waitForState,
} from './fixtures';

/** Every button LED of the panel is off and the LED is blank (§5.7, before the first gesture). */
async function expectUnlit(page: import('@playwright/test').Page) {
  await expect(page.getByTestId('led')).toHaveAttribute('data-text', '  ');
  await expect(page.getByTestId('led')).toHaveAttribute('data-dp', 'false');
  const lit = await page.locator('[data-testid^="ctl-"][data-lit="true"]').count();
  expect(lit, 'no button LED is lit before the gesture').toBe(0);
}

test.describe('AC-SC2 web fallback states', () => {
  test('AC-SC2 before the first gesture the start card shows over the full, unlit panel', async ({ page }) => {
    await openApp(page);
    const card = page.getByTestId('start-card');
    await expect(card).toBeVisible();
    await expect(card).toHaveText('Click or press any key to start VJN6');
    await expect(page.getByTestId('panel')).toBeVisible();
    await expectUnlit(page);
    expect(await page.evaluate(() => window.__midiDouble!.requests())).toEqual([]); // MIDI only after the gesture
    await startApp(page);
    await expect(page.getByTestId('led')).toHaveAttribute('data-text', '11');
    await expect(ctl(page, 'saw')).toHaveAttribute('data-lit', 'true');
    expect(await page.evaluate(() => window.__midiDouble!.requests())).toEqual([{ sysex: true }]);
  });

  test('AC-SC2 served without COOP/COEP: the isolation message replaces the UI', async ({ page }) => {
    const response = await page.goto(`${NOISO_ORIGIN}/?test`);
    expect(response!.headers()['cross-origin-opener-policy']).toBeUndefined();
    expect(await page.evaluate(() => crossOriginIsolated)).toBe(false);
    const msg = page.getByTestId('fatal-message');
    await expect(msg).toBeVisible();
    await expect(msg).toContainText('VJN6 needs to be opened from http://localhost:5173 (cross-origin isolation is off).');
    await expect(page.getByTestId('panel')).toHaveCount(0);
    await expect(page.getByTestId('start-card')).toHaveCount(0);
  });

  test.describe('requestMIDIAccess rejected', () => {
    test.use({ midiMode: 'reject' });
    test('AC-SC2 requestMIDIAccess rejected: Settings shows "MIDI not available — use the computer keyboard."', async ({
      page,
    }) => {
      await openApp(page);
      await startApp(page);
      expect(await page.evaluate(() => window.__midiDouble!.requests())).toEqual([{ sysex: true }]);
      await page.waitForFunction(() => window.__vjn6Test!.env().midiAvailable === false);
      const dlg = await openSettings(page);
      await expect(dlg.getByTestId('set-midi-state')).toHaveText('MIDI not available — use the computer keyboard.');
      await expect(dlg.getByTestId('set-midi-output')).toHaveCount(0);
    });
  });

  test.describe('granted without SysEx', () => {
    test.use({ midiMode: 'nosysex' });
    test('AC-SC2 granted without SysEx: "SysEx not permitted — SysEx disabled." and Send patch disabled', async ({
      page,
    }) => {
      await openApp(page);
      await startApp(page);
      await page.waitForFunction(() => window.__vjn6Test!.env().midiAvailable === true);
      // 106 mode with MIDI function III and the output chosen: only the missing SysEx permission blocks it
      await setMode(page, '106');
      let dlg = await openSettings(page);
      await expect(dlg.getByTestId('set-sysex-state')).toHaveText('SysEx not permitted — SysEx disabled.');
      await expect(dlg.getByTestId('set-midi-function-3')).toHaveAttribute('aria-pressed', 'true');
      await dlg.getByTestId('set-midi-output').selectOption({ label: 'VJN6 Test Synth' });
      await page.waitForFunction(() => window.__vjn6Test!.env().midiOutAvailable === true);
      await page.keyboard.press('Escape');
      dlg = await openBankManager(page);
      const send = dlg.getByTestId('bm-send-patch');
      await expect(send).toHaveAttribute('aria-disabled', 'true');
      await expect(send).toHaveAttribute('title', 'SysEx not permitted');
      await expect(send).toBeDisabled(); // aria-disabled: Playwright and assistive tech see it as disabled
      const before = (await state(page)).seq;
      await send.click({ force: true }); // a click on the disabled button sends nothing
      await page.waitForTimeout(200);
      expect(await page.evaluate(() => window.__midiDouble!.sent())).toEqual([]);
      expect((await state(page)).seq).toBe(before);
    });
  });

  test('AC-SC2 (contrast) granted with SysEx: the same setup enables Send patch, which sends one F0 41 31', async ({
    page,
  }) => {
    await openApp(page);
    await startApp(page);
    await page.waitForFunction(() => window.__vjn6Test!.env().sysexPermitted === true);
    await setMode(page, '106');
    let dlg = await openSettings(page);
    await expect(dlg.getByTestId('set-sysex-state')).toHaveCount(0);
    await dlg.getByTestId('set-midi-output').selectOption({ label: 'VJN6 Test Synth' });
    await page.waitForFunction(() => window.__vjn6Test!.env().midiOutAvailable === true);
    await page.keyboard.press('Escape');
    dlg = await openBankManager(page);
    const send = dlg.getByTestId('bm-send-patch');
    await expect(send).toHaveAttribute('aria-disabled', 'false');
    await page.evaluate(() => window.__midiDouble!.clearSent());
    await send.click();
    await expect.poll(() => page.evaluate(() => window.__midiDouble!.sent())).toHaveLength(1);
    const [msg] = await page.evaluate(() => window.__midiDouble!.sent());
    expect(msg.slice(0, 5)).toEqual([0xf0, 0x41, 0x31, 0x00, 0x00]);
    expect(msg).toHaveLength(24);
  });
});

test.describe('AC-F1.1 web start', () => {
  test('AC-F1.1 web: the start card shows first and its dismissing click activates nothing', async ({ page }) => {
    await openApp(page);
    await expect(page.getByTestId('start-card')).toBeVisible();
    await expectUnlit(page);
    const before = await state(page);
    expect(before.edit[F.PULSE]).toBe(0);

    // the click lands on the card exactly where PULSE (and, in a second step, a slider) lies under it
    const pulse = await centre(ctl(page, 'pulse'));
    await page.mouse.click(pulse.x, pulse.y);
    await expect(page.getByTestId('start-card')).toHaveCount(0);
    await page.waitForFunction(() => window.__vjn6Test!.started());
    await page.waitForTimeout(300);
    const after = await state(page);
    expect(after.edit).toEqual(before.edit);
    expect(after.perf).toEqual(before.perf);
    expect(after.slot).toBe(before.slot);
    expect(after.edited).toBe(false);
    await expect(ctl(page, 'pulse')).toHaveAttribute('data-lit', 'false');
    await expect(page.getByTestId('led')).toHaveAttribute('data-text', '11');
    // the panel itself works after the gesture
    await ctl(page, 'pulse').click();
    await waitForState(page, (s) => s.edit[F.PULSE] === 1);
  });

  test('AC-F1.1 web: a drag that starts on the card does not move the slider under it', async ({ page }) => {
    await openApp(page);
    const res = ctl(page, 'vcf-res');
    const c = await centre(res);
    await page.mouse.move(c.x, c.y);
    await page.mouse.down();
    await page.mouse.move(c.x, c.y - 60, { steps: 6 });
    await page.mouse.up();
    await page.waitForFunction(() => window.__vjn6Test!.started());
    await page.waitForTimeout(200);
    expect((await state(page)).edit[F.VCF_RES]).toBe(0);
    await expect(res).toHaveAttribute('data-value', '0');
  });

  test('AC-F1.1 web: a dismissing key is not played and not passed to the panel', async ({ page }) => {
    await openApp(page);
    await page.keyboard.down('KeyA'); // C3 on the computer keyboard
    await expect(page.getByTestId('start-card')).toHaveCount(0);
    await waitForAudio(page);
    const peak = await peakRmsOver(page, 400);
    await page.keyboard.up('KeyA');
    expect(peak, 'the dismissing key does not sound').toBeLessThan(SOUND_DB);
    const s = await state(page);
    expect(s.edited).toBe(false);
    expect(s.perf[P.HOLD]).toBe(0);
    // the next press of the same key plays
    await page.keyboard.down('KeyA');
    await expect.poll(() => peakRmsOver(page, 100), { timeout: 5_000 }).toBeGreaterThan(SOUND_DB);
    await page.keyboard.up('KeyA');
  });
});
