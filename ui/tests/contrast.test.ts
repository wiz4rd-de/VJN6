// The contrast part of AC-U3.1: the R-U3 text/background pairs in theme.css reach 4.5:1.
import { describe, expect, it } from 'vitest';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const css = readFileSync(fileURLToPath(new URL('../src/theme.css', import.meta.url)), 'utf8');

/** The custom properties of the first :root block. */
function tokens(text: string): Record<string, string> {
  const root = /:root\s*\{([^}]*)\}/.exec(text);
  if (!root) throw new Error('no :root block in theme.css');
  const out: Record<string, string> = {};
  for (const m of root[1].matchAll(/(--[\w-]+)\s*:\s*([^;]+);/g)) out[m[1]] = m[2].trim();
  return out;
}

function luminance(hex: string): number {
  const m = /^#([0-9a-f]{6})$/i.exec(hex);
  if (!m) throw new Error(`not a #rrggbb colour: ${hex}`);
  const ch = [0, 2, 4].map((i) => parseInt(m[1].slice(i, i + 2), 16) / 255);
  const lin = ch.map((c) => (c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4));
  return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2];
}

export function contrast(a: string, b: string): number {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
}

describe('AC-U3.1 contrast of the R-U3 pairs in theme.css', () => {
  const t = tokens(css);

  it('AC-U3.1 the §11 tokens are present verbatim', () => {
    expect(t['--legend']).toBe('#ECE8DC');
    expect(t['--overlay']).toBe('#24221F');
    expect(t['--tooltip']).toBe('#0E0E0E');
    expect(Object.keys(t)).toHaveLength(21);
  });

  it('AC-U3.1 overlay text (--legend on --overlay) is at least 4.5:1', () => {
    expect(contrast(t['--legend'], t['--overlay'])).toBeGreaterThanOrEqual(4.5);
  });

  it('AC-U3.1 tooltip text (--legend on --tooltip) is at least 4.5:1', () => {
    expect(contrast(t['--legend'], t['--tooltip'])).toBeGreaterThanOrEqual(4.5);
  });
});
