// AC-F10.1 through the real bank manager (§5.5, F-10, §4.3, §4.6): loads, protect, renames, row drags,
// exports, a rejected file, a two-file drop and a cancelled file dialog. Memory protect is on by default
// (F-6); "protected / not" cases switch it with the overlay's toggle.
import { readFile } from 'node:fs/promises';
import type { Locator, Page } from '@playwright/test';
import {
  bank,
  ctl,
  expect,
  F,
  openAndStart,
  openBankManager,
  P,
  state,
  test,
  waitForState,
  type CoreState,
} from './fixtures';
import { syxMessage, testBank, vjn6Bank, vjn6Patch, wavFile, type PatchSpec } from './files';

const DEFAULT_NAMES = ['INIT', 'BRASS', 'STRINGS', 'PAD', 'BASS', 'PLUCK', 'ORGAN', 'SWEEP', 'SFX'];

const TEST_PATCH: PatchSpec = {
  name: 'TEST PATCH',
  sliders: [5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80],
  switches: [2, 1, 0, 0, 3, 2, 1, 1, 2],
};

/** Picks `files` in the "Load file…" dialog (the browser's file picker on the web). */
async function loadViaDialog(page: Page, dlg: Locator, files: { name: string; buffer: Buffer }[]): Promise<void> {
  const chooser = page.waitForEvent('filechooser');
  await dlg.getByTestId('bm-load').click();
  await (await chooser).setFiles(files.map((f) => ({ ...f, mimeType: 'application/octet-stream' })));
}

async function expectNotice(dlg: Locator, text: string): Promise<void> {
  await expect(dlg.getByTestId('bm-notice')).toHaveText(text);
}

async function expectLedMessage(page: Page, text: string, tooltip: string): Promise<void> {
  await expect(page.getByTestId('led')).toHaveAttribute('data-raw-text', text);
  await expect(page.getByTestId('led-message')).toHaveText(tooltip);
}

async function setProtectInBm(page: Page, dlg: Locator, on: boolean): Promise<void> {
  const t = dlg.getByTestId('bm-protect');
  if ((await t.getAttribute('aria-pressed')) !== String(on)) await t.click();
  await waitForState(page, (s) => s.perf[P.PROTECT] === (on ? 1 : 0));
  await expect(t).toHaveText(`Memory protect: ${on ? 'ON' : 'OFF'}`);
}

function unchanged(a: CoreState, b: CoreState): void {
  expect(b.edit).toEqual(a.edit);
  expect(b.stored).toEqual(a.stored);
  expect(b.name).toBe(a.name);
  expect(b.slot).toBe(a.slot);
  expect(b.edited).toBe(a.edited);
  expect(b.manual).toBe(a.manual);
}

test.describe('AC-F10.1 bank manager', () => {
  test('AC-F10.1 Load file… a .vjn6 → edit buffer with its name, Manual, CLEAN, "Loaded …"', async ({ page }) => {
    await openAndStart(page);
    const dlg = await openBankManager(page);
    await loadViaDialog(page, dlg, [{ name: 'test-patch.vjn6', buffer: vjn6Patch(TEST_PATCH) }]);
    const s = await waitForState(page, (st) => st.name === 'TEST PATCH');
    expect(s.edit).toEqual([...TEST_PATCH.sliders, ...TEST_PATCH.switches]);
    expect(s.manual).toBe(true);
    expect(s.edited).toBe(false);
    expect(s.slot).toBe(1);
    await expectNotice(dlg, 'Loaded test-patch.vjn6');
    await expect(dlg.getByTestId('bm-edit-target')).toContainText('MANUAL');
    await expect(dlg.getByTestId('bm-edit-target')).toContainText('TEST PATCH');
    expect((await bank(page)).map((e) => e.n).slice(0, 9)).toEqual(DEFAULT_NAMES); // no slot written
    await page.keyboard.press('Escape');
    await expect(page.getByTestId('led')).toHaveAttribute('data-text', '--');
  });

  test('AC-F10.1 a .vjn6bank: protected → Pr and nothing written; not protected → all 128 slots and names', async ({
    page,
  }) => {
    await openAndStart(page);
    const before = await state(page);
    const dlg = await openBankManager(page);
    const file = { name: 'my bank.vjn6bank', buffer: vjn6Bank(testBank()) };

    await setProtectInBm(page, dlg, true);
    await loadViaDialog(page, dlg, [file]);
    await expectNotice(dlg, 'Memory is protected.');
    await expectLedMessage(page, 'Pr', 'MEMORY PROTECTED');
    expect((await bank(page)).map((e) => e.n).slice(0, 9)).toEqual(DEFAULT_NAMES);
    await expect(dlg.getByTestId('bank-name-2')).toHaveText('BRASS');

    await setProtectInBm(page, dlg, false);
    await loadViaDialog(page, dlg, [file]);
    await expectNotice(dlg, 'Loaded my bank.vjn6bank');
    await expect(dlg.getByTestId('bank-name-2')).toHaveText('BANK 2');
    const b = await bank(page);
    const spec = testBank();
    expect(b.map((e) => e.n)).toEqual(spec.map((p) => p.name));
    expect(b.map((e) => e.p)).toEqual(spec.map((p) => [...p.sliders, ...p.switches]));
    await expect(dlg.getByTestId('bank-name-128')).toHaveText('BANK 128');
    // the edit buffer, stored patch, current slot and flags are unchanged (F-10)
    unchanged(before, await state(page));
  });

  test('AC-F10.1 a one-message .syx → edit buffer named after the file, Manual; never a slot', async ({ page }) => {
    await openAndStart(page);
    const dlg = await openBankManager(page);
    const sliders = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
    // sw1 0x51: 16′, saw, chorus I; sw2 0x11: PWM MANUAL, VCF normal, VCA ENV, HPF 1 (AC-F11.1)
    const bytes = Buffer.from(syxMessage(0x30, 9, sliders, 0x51, 0x11));
    await loadViaDialog(page, dlg, [{ name: 'One Shot.syx', buffer: bytes }]);
    const s = await waitForState(page, (st) => st.name === 'One Shot');
    expect(s.edit).toEqual([...sliders, 0, 0, 1, 1, 1, 1, 0, 0, 1]);
    expect(s.manual).toBe(true);
    expect(s.edited).toBe(false);
    expect(s.slot).toBe(1);
    await expectNotice(dlg, 'Loaded One Shot.syx');
    expect((await bank(page))[9].n).toBe('INIT'); // pp 9 did not write slot 10
  });

  test('AC-F10.1 rename a row to é😀abcdefghijklm: protected → Pr, unchanged; not → ??abcdefghij', async ({ page }) => {
    await openAndStart(page);
    const dlg = await openBankManager(page);
    const typed = 'é😀abcdefghijklm';

    async function rename(slot: number) {
      await dlg.getByTestId(`bank-name-${slot}`).dblclick();
      const input = dlg.getByTestId('bank-rename');
      await expect(input).toBeFocused();
      await input.fill(typed);
      await input.press('Enter');
      await expect(input).toHaveCount(0);
    }

    await setProtectInBm(page, dlg, true);
    await rename(3);
    await expectNotice(dlg, 'Memory is protected.');
    await expectLedMessage(page, 'Pr', 'MEMORY PROTECTED');
    await expect(dlg.getByTestId('bank-name-3')).toHaveText('STRINGS');
    expect((await bank(page))[2].n).toBe('STRINGS');

    await setProtectInBm(page, dlg, false);
    await rename(3);
    await expect(dlg.getByTestId('bank-name-3')).toHaveText('??abcdefghij');
    expect((await bank(page))[2].n).toBe('??abcdefghij');
    // renaming the current slot also renames the edit buffer (§4.3)
    await rename(1);
    await waitForState(page, (s) => s.name === '??abcdefghij');
    expect((await bank(page))[0].n).toBe('??abcdefghij');
    // Esc reverts a rename in progress
    await dlg.getByTestId('bank-name-4').dblclick();
    await dlg.getByTestId('bank-rename').fill('NOPE');
    await dlg.getByTestId('bank-rename').press('Escape');
    await expect(dlg.getByTestId('bank-name-4')).toHaveText('PAD');
  });

  test('AC-F10.1 drag row 2 → 9 copies patch and name; drag a row → the edit buffer loads it, Manual', async ({
    page,
  }) => {
    await openAndStart(page);
    const before = await state(page);
    const dlg = await openBankManager(page);
    await setProtectInBm(page, dlg, false);
    const b0 = await bank(page);

    await dlg.getByTestId('bank-slot-2').dragTo(dlg.getByTestId('bank-row-9'));
    await expect(dlg.getByTestId('bank-name-9')).toHaveText('BRASS');
    const b1 = await bank(page);
    expect(b1[8]).toEqual(b0[1]);
    expect(b1.filter((_, i) => i !== 8)).toEqual(b0.filter((_, i) => i !== 8));
    unchanged(before, await state(page)); // edit buffer, stored patch and current slot unchanged

    await dlg.getByTestId('bank-disp-4').dragTo(dlg.getByTestId('bm-edit-target'));
    const s = await waitForState(page, (st) => st.name === 'PAD' && st.manual);
    expect(s.edit).toEqual(b0[3].p);
    expect(s.slot).toBe(1);
    expect(await bank(page)).toEqual(b1);
    await expect(dlg.getByTestId('bm-edit-target')).toContainText('MANUAL');

    // with memory protect on, a row drop is refused
    await setProtectInBm(page, dlg, true);
    await dlg.getByTestId('bank-slot-3').dragTo(dlg.getByTestId('bank-row-10'));
    await expectNotice(dlg, 'Memory is protected.');
    await expectLedMessage(page, 'Pr', 'MEMORY PROTECTED');
    expect((await bank(page))[9].n).toBe('INIT');
  });

  test('AC-F10.1 export bank and patch in both formats: downloads with the §4.6 names', async ({ page }) => {
    await openAndStart(page);
    await ctl(page, 'patch-2').click(); // BRASS
    const s = await waitForState(page, (st) => st.name === 'BRASS');
    const dlg = await openBankManager(page);
    const b = await bank(page);

    // the file name VJN6 offers is the anchor's `download` attribute; the browser may sanitise it when it
    // saves (WebKit on Linux turns spaces into `_`), which is outside VJN6
    await page.evaluate(() => {
      const offered: string[] = [];
      (window as unknown as { __offered: string[] }).__offered = offered;
      const click = HTMLAnchorElement.prototype.click;
      HTMLAnchorElement.prototype.click = function (this: HTMLAnchorElement) {
        if (this.download) offered.push(this.download);
        return click.call(this);
      };
    });

    async function download(testid: string) {
      const dl = page.waitForEvent('download');
      await dlg.getByTestId(testid).click();
      const d = await dl;
      const name = await page.evaluate(() => (window as unknown as { __offered: string[] }).__offered.at(-1)!);
      expect([name, name.replace(/ /g, '_')], 'the browser saves the offered name').toContain(d.suggestedFilename());
      return { name, bytes: await readFile((await d.path())!) };
    }

    const bankJson = await download('bm-export-bank-json');
    expect(bankJson.name).toBe('VJN6 bank.vjn6bank');
    const parsed = JSON.parse(bankJson.bytes.toString('utf8'));
    expect(parsed.vjn6).toBe(1);
    expect(parsed.patches).toHaveLength(128);
    expect(parsed.patches.map((p: { name: string }) => p.name)).toEqual(b.map((e) => e.n));
    expect(bankJson.bytes.toString('utf8').endsWith('}\n')).toBe(true); // canonical: LF, final newline

    const bankSyx = await download('bm-export-bank-syx');
    expect(bankSyx.name).toBe('VJN6 bank.syx');
    expect(bankSyx.bytes.length).toBe(128 * 24);
    for (let i = 0; i < 128; i++) {
      const m = bankSyx.bytes.subarray(i * 24, i * 24 + 24);
      expect([m[0], m[1], m[2], m[3], m[4], m[23]]).toEqual([0xf0, 0x41, 0x30, 0x00, i, 0xf7]);
    }

    const patchJson = await download('bm-export-patch-json');
    expect(patchJson.name).toBe('BRASS.vjn6');
    const pj = JSON.parse(patchJson.bytes.toString('utf8'));
    expect(pj.vjn6).toBe(1);
    expect(pj.name).toBe('BRASS');
    expect(pj.sliders).toEqual(s.edit.slice(0, 16));

    const patchSyx = await download('bm-export-patch-syx');
    expect(patchSyx.name).toBe('BRASS.syx');
    expect(patchSyx.bytes.length).toBe(24);
    expect([...patchSyx.bytes.subarray(0, 5)]).toEqual([0xf0, 0x41, 0x31, 0x00, 0x00]);
    expect([...patchSyx.bytes.subarray(5, 21)]).toEqual(s.edit.slice(0, 16));
    expect(patchSyx.bytes[23]).toBe(0xf7);
  });

  test('AC-F10.1 a .wav → "Not a VJN6 or Juno-106 file: …" and Er', async ({ page }) => {
    await openAndStart(page);
    const before = await state(page);
    const dlg = await openBankManager(page);
    await loadViaDialog(page, dlg, [{ name: 'kick drum.wav', buffer: wavFile() }]);
    await expectNotice(dlg, 'Not a VJN6 or Juno-106 file: kick drum.wav');
    await expectLedMessage(page, 'Er', 'FORMAT CONFLICT');
    unchanged(before, await state(page));
    expect((await bank(page)).map((e) => e.n).slice(0, 9)).toEqual(DEFAULT_NAMES);
  });

  test('AC-F10.1 two files dropped on the window → only the first is used; the bank manager opens', async ({ page }) => {
    await openAndStart(page);
    await expect(page.getByTestId('bank-manager')).toHaveCount(0);
    const first = vjn6Patch({ ...TEST_PATCH, name: 'FIRST' }).toString('utf8');
    const second = vjn6Patch({ ...TEST_PATCH, name: 'SECOND', sliders: TEST_PATCH.sliders.map((v) => v + 1) }).toString(
      'utf8',
    );
    const dt = await page.evaluateHandle(
      ({ first, second }) => {
        const d = new DataTransfer();
        d.items.add(new File([first], 'first.vjn6'));
        d.items.add(new File([second], 'second.vjn6'));
        return d;
      },
      { first, second },
    );
    const target = page.getByTestId('panel');
    await target.dispatchEvent('dragenter', { dataTransfer: dt });
    await target.dispatchEvent('dragover', { dataTransfer: dt });
    await target.dispatchEvent('drop', { dataTransfer: dt });
    const dlg = page.getByTestId('bank-manager');
    await expect(dlg).toBeVisible();
    const s = await waitForState(page, (st) => st.name === 'FIRST');
    expect(s.edit.slice(0, 16)).toEqual(TEST_PATCH.sliders);
    expect(s.manual).toBe(true);
    await expectNotice(dlg, 'Loaded first.vjn6');
    await page.waitForTimeout(300);
    expect((await state(page)).name, 'the second file is ignored').toBe('FIRST');
  });

  test('AC-F10.1 Cancel in the file dialog changes nothing', async ({ page }) => {
    await openAndStart(page);
    await ctl(page, 'patch-3').click();
    await waitForState(page, (s) => s.slot === 3);
    const before = await state(page);
    const bankBefore = await bank(page);
    const dlg = await openBankManager(page);
    await loadViaDialog(page, dlg, []); // the picker closes without a file
    await page.waitForTimeout(400);
    const after = await state(page);
    unchanged(before, after);
    expect(after.led).toEqual(before.led);
    expect(await bank(page)).toEqual(bankBefore);
    await expect(dlg.getByTestId('bm-notice')).toHaveText('');
    await expect(dlg).toBeVisible();
    expect(after.edit[F.VCF_FREQ]).toBe(before.edit[F.VCF_FREQ]);
  });
});
