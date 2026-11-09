// AC-B2: `npm run web` serves the page on http://localhost:5173 with COOP/COEP; after the start card,
// a computer-keyboard note sounds (analyser RMS > −60 dBFS); in Chromium a Web MIDI note does too.
import { ORIGIN, SPEC_ORIGIN } from './env';
import { expect, midi, openApp, setChorusNoiseOff, SOUND_DB, startApp, test, waitForRms } from './fixtures';

test('AC-B2 served with COOP/COEP; a key and (Chromium) a mocked Web MIDI note sound after the start card', async ({
  page,
  browserName,
}) => {
  test.info().annotations.push({ type: 'origin', description: ORIGIN });
  if (ORIGIN !== SPEC_ORIGIN) {
    test.info().annotations.push({ type: 'note', description: `served on ${ORIGIN} (VJN6_E2E_PORT), not ${SPEC_ORIGIN}` });
  }

  const response = await page.goto('/?test');
  expect(response, 'the page loads').not.toBeNull();
  expect(new URL(page.url()).origin).toBe(ORIGIN);
  const headers = response!.headers();
  expect(headers['cross-origin-opener-policy']).toBe('same-origin');
  expect(headers['cross-origin-embedder-policy']).toBe('require-corp');
  expect(await page.evaluate(() => crossOriginIsolated)).toBe(true);

  await openApp(page);
  await startApp(page, { audio: true });
  // slot 1 (`Strings 1`) has chorus I, whose hiss is close to the threshold: chorus noise off (§4.4)
  await setChorusNoiseOff(page);
  await waitForRms(page, 'below', SOUND_DB);

  // computer keyboard: A = C3 (MIDI 60) on the first-run patch, slot 1 `Strings 1` (§15.3)
  await page.keyboard.down('KeyA');
  const keyDb = await waitForRms(page, 'above', SOUND_DB);
  await page.keyboard.up('KeyA');
  test.info().annotations.push({ type: 'rms', description: `key A: ${keyDb.toFixed(1)} dBFS` });
  await waitForRms(page, 'below', SOUND_DB);

  if (browserName === 'chromium') {
    const m = midi(page);
    expect(await m.requests()).toEqual([{ sysex: true }]);
    await m.waitListening();
    await m.inject([0x90, 60, 100]);
    const midiDb = await waitForRms(page, 'above', SOUND_DB);
    await m.inject([0x80, 60, 0]);
    test.info().annotations.push({ type: 'rms', description: `MIDI note 60: ${midiDb.toFixed(1)} dBFS` });
    await waitForRms(page, 'below', SOUND_DB);
  }
});
