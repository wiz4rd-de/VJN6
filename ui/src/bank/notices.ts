// Bank-manager notices (§5.5) and the middle ellipsis for long names (§5.5, §8).

import type { NoticeKind } from '../protocol';

const ELLIPSIS = '…';

/**
 * Shortens text longer than `max` characters (code points) in the middle with `…`, so that the
 * result is exactly `max` characters long.
 */
export function shortenMiddle(text: string, max = 40): string {
  const chars = Array.from(text);
  if (chars.length <= max) return text;
  const keep = max - 1;
  const head = Math.ceil(keep / 2);
  const tail = keep - head;
  return chars.slice(0, head).join('') + ELLIPSIS + chars.slice(chars.length - tail).join('');
}

/** The notice text for a core (or host) notice; "" for kinds that show no notice. */
export function noticeText(kind: NoticeKind, file: string, i = 0, j = 0): string {
  const f = shortenMiddle(file ?? '');
  switch (kind) {
    case 'malformed':
      return `Not a VJN6 or Juno-106 file: ${f}`;
    case 'unreadable':
      return `Could not read ${f}`;
    case 'protected':
      return 'Memory is protected.';
    case 'imported':
      return `${i} patches imported, ${j} dropped`;
    case 'loaded':
    case 'bankLoaded':
      return `Loaded ${f}`;
    case 'writeFailed':
      return `Could not write ${f}`;
    default:
      return '';
  }
}
