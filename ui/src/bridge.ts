// UI <-> core bridge (docs/BRIDGE.md). Two transports, chosen at runtime:
// - desktop (plugin and standalone): JUCE WebBrowserComponent native integration. UI -> host via
//   the native function `vjn6Command(json)`; host -> UI via the event `vjn6Events`, whose payload is
//   a JSON array of message strings.
// - web: the WASM module (src/web/transport.ts).

import { HOST_ONLY, parseBatch, type CoreMessage, type UiMessage } from './protocol';

export type MessageSink = (msgs: CoreMessage[]) => void;

export interface Transport {
  readonly kind: 'desktop' | 'web';
  /** Connects and sends `ready`; core messages then flow to the sink. */
  connect(sink: MessageSink): void | Promise<void>;
  send(msg: UiMessage): void;
  /** File bytes from the file picker or a drop (§5.5, F-10). */
  loadFile(name: string, bytes: Uint8Array): void;
}

/** The part of JUCE's injected `window.__JUCE__` object that the UI uses. */
interface JuceBackend {
  addEventListener(eventId: string, fn: (payload: unknown) => void): unknown;
  emitEvent(eventId: string, payload: unknown): void;
}
interface JuceGlobal {
  backend?: JuceBackend;
  initialisationData?: { __juce__functions?: string[] };
}

export const NATIVE_FUNCTION = 'vjn6Command';
export const NATIVE_EVENT = 'vjn6Events';

function juceGlobal(w: unknown): JuceGlobal | undefined {
  return (w as { __JUCE__?: JuceGlobal } | undefined)?.__JUCE__;
}

/** True inside the desktop WebView (JUCE native integration with the vjn6Command function). */
export function hasJuceBackend(w: unknown = typeof window !== 'undefined' ? window : undefined): boolean {
  const j = juceGlobal(w);
  if (!j?.backend || typeof j.backend.emitEvent !== 'function') return false;
  const fns = j.initialisationData?.__juce__functions;
  return !Array.isArray(fns) || fns.includes(NATIVE_FUNCTION);
}

export function base64(bytes: Uint8Array): string {
  let s = '';
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    s += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(s);
}

export function unbase64(b64: string): Uint8Array {
  const s = atob(b64);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; ++i) out[i] = s.charCodeAt(i);
  return out;
}

/** Desktop transport over JUCE's WebBrowserComponent native integration. */
export class DesktopTransport implements Transport {
  readonly kind = 'desktop' as const;
  private backend: JuceBackend;
  private resultId = 1;

  constructor(w: unknown = window) {
    const b = juceGlobal(w)?.backend;
    if (!b) throw new Error('JUCE backend not available');
    this.backend = b;
  }

  connect(sink: MessageSink): void {
    this.backend.addEventListener(NATIVE_EVENT, (payload: unknown) => {
      const msgs = parseBatch(payload);
      if (msgs.length) sink(msgs);
    });
    this.send({ t: 'ready' });
  }

  send(msg: UiMessage): void {
    // Equivalent of JUCE's getNativeFunction(name)(arg): an `__juce__invoke` event. The completion
    // (`__juce__complete`) carries no data the UI needs.
    this.backend.emitEvent('__juce__invoke', {
      name: NATIVE_FUNCTION,
      params: [JSON.stringify(msg)],
      resultId: this.resultId++,
    });
  }

  loadFile(name: string, bytes: Uint8Array): void {
    this.send({ t: 'loadFile', name, b64: base64(bytes) });
  }
}

/** Host-only messages are not core commands (docs/BRIDGE.md). */
export function isHostOnly(msg: UiMessage): boolean {
  return HOST_ONLY.has(msg.t);
}
