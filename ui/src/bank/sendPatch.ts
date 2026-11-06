// Send patch availability (§5.5): enabled only in 106 mode with MIDI function III and an available
// MIDI output that may carry SysEx; otherwise the reason is shown as the button's tooltip.

import { Perf, type CoreState, type EnvInfo } from '../protocol';

export const REASON_MODE = '106 mode, MIDI function III only';
export const REASON_OUTPUT = 'MIDI output not available';
export const REASON_SYSEX = 'SysEx not permitted';

/** "" when Send patch is enabled, else the reason it is disabled. */
export function sendPatchBlockReason(core: CoreState | null, env: EnvInfo): string {
  if (!core || core.mode !== 1 || core.perf[Perf.MIDI_FUNCTION] !== 3) return REASON_MODE;
  // MIDI that is granted without SysEx cannot send a patch whatever output is chosen
  if (env.midiAvailable && !env.sysexPermitted) return REASON_SYSEX;
  if (!env.midiOutAvailable) return REASON_OUTPUT;
  if (!env.sysexPermitted) return REASON_SYSEX;
  return '';
}
