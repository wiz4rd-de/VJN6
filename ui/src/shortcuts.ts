// Shortcuts (R-U2): Alt (Option on macOS) + B / , / M / W / 1…8, and Esc for overlays. Matched by
// KeyboardEvent.code so that Option's dead keys and layouts do not matter. Shortcuts never play
// notes: the computer keyboard ignores every event with Alt held.

import { isTextField } from './keyboard';

export interface ShortcutActions {
  toggleBankManager(): void;
  toggleSettings(): void;
  /** Closes the open overlay; returns false if none is open. */
  closeOverlay(): boolean;
  manual(): void;
  write(): void;
  patch(i: number): void;
}

/** Handles one keydown; returns true if it was a shortcut (and was consumed). */
export function handleShortcut(e: KeyboardEvent, a: ShortcutActions): boolean {
  if (e.isComposing) return false;
  if (e.code === 'Escape' && !e.altKey && !e.ctrlKey && !e.metaKey) {
    if (isTextField(e.target)) return false; // the field reverts its own edit
    if (a.closeOverlay()) {
      e.preventDefault();
      return true;
    }
    return false;
  }
  if (!e.altKey || e.ctrlKey || e.metaKey) return false;
  if (isTextField(e.target)) return false;
  let act: (() => void) | null = null;
  switch (e.code) {
    case 'KeyB':
      act = () => a.toggleBankManager();
      break;
    case 'Comma':
      act = () => a.toggleSettings();
      break;
    case 'KeyM':
      act = () => a.manual();
      break;
    case 'KeyW':
      act = () => a.write();
      break;
    default: {
      const m = /^Digit([1-8])$/.exec(e.code);
      if (m) {
        const i = Number(m[1]);
        act = () => a.patch(i);
      }
    }
  }
  if (!act) return false;
  e.preventDefault();
  if (!e.repeat) act();
  return true;
}
