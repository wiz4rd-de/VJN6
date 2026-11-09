// Pointer, wheel and keyboard handling shared by sliders and knobs (R-U1, R-U1a).
// Distances are logical (canvas) pixels: screen pixels divided by the current scale.

export interface ValueAccess {
  get(): number;
  set(v: number): void;
  min: number;
  max: number;
}

/** Pixels per byte while dragging: 2 normally, 8 with Ctrl/Cmd (R-U1). */
export const PX_PER_STEP = 2;
export const PX_PER_STEP_FINE = 8;

export function clamp(v: number, lo: number, hi: number): number {
  return v < lo ? lo : v > hi ? hi : v;
}

/**
 * Starts a vertical relative drag on pointerdown. Up increases. `pxPerStep` overrides the step
 * size (the 106 HPF slider snaps to its 4 positions). Calls `end` once on release.
 */
export function startVerticalDrag(
  e: PointerEvent,
  el: HTMLElement,
  access: ValueAccess,
  scale: () => number,
  opts: { pxPerStep?: (fine: boolean) => number; begin?: () => void; end?: () => void } = {},
): void {
  if (e.button !== 0) return;
  e.preventDefault();
  el.focus({ preventScroll: true });
  try {
    el.setPointerCapture(e.pointerId);
  } catch {
    // capture may fail for synthetic events; window listeners below still track the drag
  }
  let lastY = e.clientY;
  let acc = 0;
  let cur = access.get();
  opts.begin?.();
  const move = (ev: PointerEvent) => {
    if (ev.pointerId !== e.pointerId) return;
    const dy = (ev.clientY - lastY) / Math.max(scale(), 0.01);
    lastY = ev.clientY;
    const fine = ev.ctrlKey || ev.metaKey;
    const per = opts.pxPerStep ? opts.pxPerStep(fine) : fine ? PX_PER_STEP_FINE : PX_PER_STEP;
    acc += -dy / per;
    const steps = Math.trunc(acc);
    if (steps === 0) return;
    acc -= steps;
    const next = clamp(cur + steps, access.min, access.max);
    if (next !== cur) {
      cur = next;
      access.set(cur);
    }
  };
  const up = (ev: PointerEvent) => {
    if (ev.pointerId !== e.pointerId) return;
    done();
  };
  const done = () => {
    window.removeEventListener('pointermove', move);
    window.removeEventListener('pointerup', up);
    window.removeEventListener('pointercancel', up);
    window.removeEventListener('blur', done);
    opts.end?.();
  };
  window.addEventListener('pointermove', move);
  window.addEventListener('pointerup', up);
  window.addEventListener('pointercancel', up);
  window.addEventListener('blur', done);
}

/** Wheel: ±1 per event, Shift ±10 (R-U1). Returns the delta applied. */
export function wheelStep(e: WheelEvent): number {
  const d = e.deltaY !== 0 ? e.deltaY : e.deltaX;
  if (d === 0) return 0;
  return (d < 0 ? 1 : -1) * (e.shiftKey ? 10 : 1);
}

/** Svelte action: a non-passive wheel listener (so the page does not scroll). */
export function wheel(node: HTMLElement, handler: (e: WheelEvent) => void) {
  let h = handler;
  const listener = (e: WheelEvent) => h(e);
  node.addEventListener('wheel', listener, { passive: false });
  return {
    update(next: (e: WheelEvent) => void) {
      h = next;
    },
    destroy() {
      node.removeEventListener('wheel', listener);
    },
  };
}

/**
 * Keyboard on a focused slider or knob (R-U1a): Up/Right +1, Down/Left −1, Shift ±10, Home/End.
 * Returns the new value, or null if the key is not handled.
 */
export function keyStep(e: KeyboardEvent, v: number, min: number, max: number): number | null {
  if (e.altKey || e.ctrlKey || e.metaKey) return null;
  const step = e.shiftKey ? 10 : 1;
  switch (e.key) {
    case 'ArrowUp':
    case 'ArrowRight':
      return clamp(v + step, min, max);
    case 'ArrowDown':
    case 'ArrowLeft':
      return clamp(v - step, min, max);
    case 'Home':
      return min;
    case 'End':
      return max;
    default:
      return null;
  }
}
