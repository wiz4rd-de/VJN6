<script lang="ts">
  // The bender lever (R-U1): mouse-down anywhere starts a drag; full deflection at ±60 px from the
  // mouse-down point; springs back on release (also outside the window, on blur and editor close).
  // 106: a mouse-down in the upper half also holds push until release. Shows the last value from
  // any source. Focused: Left/Right deflect fully while held (R-U1a).
  import { app } from '../model.svelte';
  import type { LeverDef } from './layout';

  let { def, is106 }: { def: LeverDef; is106: boolean } = $props();

  const FULL = 8191;
  const RANGE_PX = 60;

  let dragging = $state(false);
  let local = $state(0);
  let pushing = $state(false);
  let keys: string[] = [];
  let el: HTMLDivElement | undefined = $state();
  let cleanup: (() => void) | null = null;

  const shown = $derived(dragging ? local : (app.core?.lever ?? 0));
  const cx = $derived(def.w / 2);
  const handleX = $derived(cx + (shown / FULL) * RANGE_PX);
  const pushShown = $derived(dragging ? pushing : !!app.core?.push);
  const tooltip = () => `${def.aria} ${(shown / FULL).toFixed(2)}`;

  function showTip() {
    app.showTip({ owner: def.id, text: tooltip, x: def.x + def.w / 2, y: def.y + def.h + 20, top: def.y });
  }

  function onPointerDown(e: PointerEvent) {
    if (e.button !== 0 || !el) return;
    e.preventDefault();
    el.focus({ preventScroll: true });
    const rect = el.getBoundingClientRect();
    const scale = Math.max(app.scale, 0.01);
    const yLocal = (e.clientY - rect.top) / scale;
    const x0 = e.clientX;
    dragging = true;
    local = 0;
    pushing = is106 && yLocal < def.h / 2;
    app.send(pushing ? { t: 'lever', v: 0, drag: true, push: true } : { t: 'lever', v: 0, drag: true });
    const move = (ev: PointerEvent) => {
      if (ev.pointerId !== e.pointerId) return;
      const dx = (ev.clientX - x0) / Math.max(app.scale, 0.01);
      const v = Math.round(Math.max(-1, Math.min(1, dx / RANGE_PX)) * FULL);
      if (v !== local) {
        local = v;
        app.send({ t: 'lever', v, drag: true });
      }
    };
    const up = (ev: Event) => {
      if (ev instanceof PointerEvent && ev.pointerId !== e.pointerId) return;
      release();
    };
    window.addEventListener('pointermove', move);
    window.addEventListener('pointerup', up);
    window.addEventListener('pointercancel', up);
    window.addEventListener('blur', up);
    window.addEventListener('pagehide', up);
    cleanup = () => {
      window.removeEventListener('pointermove', move);
      window.removeEventListener('pointerup', up);
      window.removeEventListener('pointercancel', up);
      window.removeEventListener('blur', up);
      window.removeEventListener('pagehide', up);
    };
  }

  function release() {
    if (!dragging) return;
    cleanup?.();
    cleanup = null;
    const wasPushing = pushing;
    dragging = false;
    pushing = false;
    local = 0;
    app.send(wasPushing ? { t: 'lever', v: 0, drag: false, push: false } : { t: 'lever', v: 0, drag: false });
  }

  $effect(() => () => {
    release(); // editor close / unmount while dragging
    app.hideTip(def.id);
  });

  function sendKeys() {
    const last = keys[keys.length - 1];
    const v = last === 'ArrowLeft' ? -FULL : last === 'ArrowRight' ? FULL : 0;
    app.send({ t: 'lever', v, drag: false, src: 'panel' });
  }

  function onKeyDown(e: KeyboardEvent) {
    if (e.key !== 'ArrowLeft' && e.key !== 'ArrowRight') return;
    if (e.altKey || e.ctrlKey || e.metaKey) return;
    e.preventDefault();
    if (e.repeat || keys.includes(e.key)) return;
    keys = [...keys, e.key];
    sendKeys();
  }

  function onKeyUp(e: KeyboardEvent) {
    if (!keys.includes(e.key)) return;
    e.preventDefault();
    keys = keys.filter((k) => k !== e.key);
    sendKeys();
  }

  function onBlur() {
    app.hideTip(def.id);
    if (keys.length) {
      keys = [];
      sendKeys();
    }
  }
</script>

<div
  bind:this={el}
  class="lever"
  role="slider"
  tabindex="0"
  aria-label={def.aria}
  aria-orientation="horizontal"
  aria-valuemin={-FULL}
  aria-valuemax={FULL}
  aria-valuenow={shown}
  aria-valuetext={tooltip()}
  data-testid="ctl-{def.id}"
  data-value={shown}
  data-push={pushShown}
  data-dragging={dragging}
  style="left:{def.x}px; top:{def.y}px; width:{def.w}px; height:{def.h}px"
  onpointerdown={onPointerDown}
  onkeydown={onKeyDown}
  onkeyup={onKeyUp}
  onpointerenter={showTip}
  onpointerleave={() => app.hideTip(def.id)}
  onfocus={showTip}
  onblur={onBlur}
>
  <svg width={def.w} height={def.h} viewBox="0 0 {def.w} {def.h}" aria-hidden="true">
    <rect class="plate" x="0.5" y="0.5" width={def.w - 1} height={def.h - 1} rx="4" />
    {#if is106}
      <rect class="push-zone" class:on={pushShown} x="1" y="1" width={def.w - 2} height={def.h / 2 - 1} rx="3" />
      <text class="push-text" x={def.w - 6} y="11" text-anchor="end">PUSH</text>
    {/if}
    <rect class="slot" x={cx - RANGE_PX - 4} y={def.h / 2 - 3} width={2 * RANGE_PX + 8} height="6" rx="3" />
    <line class="centre" x1={cx} x2={cx} y1={def.h / 2 - 9} y2={def.h / 2 + 9} />
    <g transform="translate({handleX} 0)">
      <rect class="handle" x="-7" y={def.h / 2 - 20} width="14" height="40" rx="3" />
      <rect class="grip" x="-1" y={def.h / 2 - 16} width="2" height="32" />
    </g>
  </svg>
</div>
<div class="label" aria-hidden="true" style="left:{def.x}px; top:{def.y + def.h + 4}px; width:{def.w}px">
  {def.label}
</div>

<style>
  .lever {
    position: absolute;
    cursor: ew-resize;
    touch-action: none;
  }
  .lever svg {
    display: block;
  }
  .plate {
    fill: rgba(0, 0, 0, 0.25);
    stroke: rgba(169, 164, 154, 0.45);
    stroke-width: 1;
  }
  .push-zone {
    fill: rgba(236, 232, 220, 0.05);
  }
  .push-zone.on {
    fill: rgba(255, 90, 72, 0.18);
  }
  .push-text {
    font-family: 'Barlow Condensed', sans-serif;
    font-size: 9px;
    font-weight: 600;
    fill: var(--legend-dim);
  }
  .slot {
    fill: var(--slider-track);
  }
  .centre {
    stroke: var(--legend-dim);
    stroke-width: 1;
  }
  .handle {
    fill: var(--slider-cap);
    stroke: #000;
    stroke-width: 1;
  }
  .grip {
    fill: #ffffff;
  }
  .label {
    position: absolute;
    text-align: center;
    font-size: 11px;
    line-height: 12px;
    font-weight: 600;
    letter-spacing: 0.03em;
    color: var(--legend);
    pointer-events: none;
  }
</style>
