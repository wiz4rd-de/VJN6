// The two skins (§11), chosen by mode and sharing one geometry. Colour assignments are visual
// latitude (§14); every colour is a §11 token.

import type { CapColor, SectionId } from '../panel/layout';

export interface Skin {
  panel: string;
  cheek: 'wood' | 'black';
  /** Header band colour per section box (memory row and right edge have none). */
  header: Partial<Record<SectionId, string>>;
  /** Box outline. */
  border: Partial<Record<SectionId, string>>;
  legend: string;
  dim: string;
  knob: string;
  knobPointer: string;
}

const DARK_TEXT = '#141414';

export const SKIN_60: Skin = {
  panel: 'var(--panel-60)',
  cheek: 'wood',
  header: {
    controller: 'var(--cap-grey)',
    lfo: 'var(--cap-orange)',
    dco: 'var(--cap-yellow)',
    hpf: 'var(--cap-orange)',
    vcf: 'var(--box-red)',
    vca: 'var(--cap-yellow)',
    env: 'var(--cap-orange)',
    chorus: 'var(--box-blue)',
  },
  border: {
    controller: 'var(--legend-dim)',
    lfo: 'var(--legend-dim)',
    dco: 'var(--legend-dim)',
    hpf: 'var(--legend-dim)',
    vcf: 'var(--legend-dim)',
    vca: 'var(--legend-dim)',
    env: 'var(--legend-dim)',
    chorus: 'var(--legend-dim)',
    memory: 'var(--legend-dim)',
    right: 'var(--legend-dim)',
  },
  legend: 'var(--legend)',
  dim: 'var(--legend-dim)',
  knob: '#1E1E1E',
  knobPointer: 'var(--legend)',
};

export const SKIN_106: Skin = {
  panel: 'var(--panel-106)',
  cheek: 'black',
  header: {
    controller: 'var(--box-white)',
    lfo: 'var(--box-red)',
    dco: 'var(--box-red)',
    hpf: 'var(--box-blue)',
    vcf: 'var(--box-blue)',
    vca: 'var(--box-white)',
    env: 'var(--box-white)',
    chorus: 'var(--box-blue)',
  },
  border: {
    controller: 'var(--box-white)',
    lfo: 'var(--box-red)',
    dco: 'var(--box-red)',
    hpf: 'var(--box-blue)',
    vcf: 'var(--box-blue)',
    vca: 'var(--box-white)',
    env: 'var(--box-white)',
    chorus: 'var(--box-blue)',
    memory: 'var(--box-white)',
    right: 'var(--box-white)',
  },
  legend: 'var(--legend)',
  dim: 'var(--legend-dim)',
  knob: '#1A1B1D',
  knobPointer: 'var(--legend)',
};

export function skinFor(is106: boolean): Skin {
  return is106 ? SKIN_106 : SKIN_60;
}

/** Text colour on a header band: dark on the light tokens, the legend colour on red and blue. */
export function headerText(color: string | undefined): string {
  if (!color) return 'var(--legend)';
  return color.includes('red') || color.includes('blue') ? 'var(--legend)' : DARK_TEXT;
}

export const CAP_FILL: Record<CapColor, string> = {
  orange: 'var(--cap-orange)',
  yellow: 'var(--cap-yellow)',
  grey: 'var(--cap-grey)',
  white: 'var(--cap-white)',
  blue: 'var(--cap-blue)',
};
