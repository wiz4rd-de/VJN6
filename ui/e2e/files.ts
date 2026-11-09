// Fixture files for the bank-manager tests, built in the formats of SPEC §4.6 / §4.5.

/** Switch fields in the §4.6 key order (W0–W8). */
export const SWITCH_KEYS = ['range', 'pulse', 'saw', 'sub', 'chorus', 'pwm_src', 'vcf_pol', 'vca_mode', 'hpf'] as const;

export interface PatchSpec {
  name: string;
  sliders: number[]; // 16 bytes B0–B15
  switches: number[]; // 9 fields W0–W8
}

function patchObject(p: PatchSpec): Record<string, unknown> {
  return {
    name: p.name,
    sliders: p.sliders,
    switches: Object.fromEntries(SWITCH_KEYS.map((k, i) => [k, p.switches[i]])),
  };
}

/** A `.vjn6` patch file. */
export function vjn6Patch(p: PatchSpec): Buffer {
  return Buffer.from(JSON.stringify({ vjn6: 1, ...patchObject(p) }, null, 2) + '\n');
}

/** A `.vjn6bank` file of 128 patches. */
export function vjn6Bank(patches: PatchSpec[]): Buffer {
  if (patches.length !== 128) throw new Error('a bank has 128 patches');
  return Buffer.from(JSON.stringify({ vjn6: 1, patches: patches.map(patchObject) }, null, 2) + '\n');
}

/** One 24-byte Juno-106 patch message: `F0 41 30|31 0n pp d0…d15 sw1 sw2 F7`. */
export function syxMessage(type: 0x30 | 0x31, pp: number, sliders: number[], sw1: number, sw2: number): number[] {
  return [0xf0, 0x41, type, 0x00, pp, ...sliders, sw1, sw2, 0xf7];
}

/** The 128 patches of a test bank: names `BANK 1`…`BANK 128`, distinct slider bytes. */
export function testBank(): PatchSpec[] {
  return Array.from({ length: 128 }, (_, i) => ({
    name: `BANK ${i + 1}`,
    sliders: Array.from({ length: 16 }, (_, b) => (i + b * 7) % 128),
    switches: [i % 3, i % 2, 1, 1, i % 4, i % 3, 0, i % 2, i % 4],
  }));
}

/** A tiny RIFF/WAVE file (not a VJN6 or Juno-106 file). */
export function wavFile(): Buffer {
  const b = Buffer.alloc(44 + 8);
  b.write('RIFF', 0, 'ascii');
  b.writeUInt32LE(36 + 8, 4);
  b.write('WAVEfmt ', 8, 'ascii');
  b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); // PCM
  b.writeUInt16LE(1, 22); // mono
  b.writeUInt32LE(48000, 24);
  b.writeUInt32LE(96000, 28);
  b.writeUInt16LE(2, 32);
  b.writeUInt16LE(16, 34);
  b.write('data', 36, 'ascii');
  b.writeUInt32LE(8, 40);
  return b;
}
