<script lang="ts">
  // A 20 × 300 slider with a 0–10 tick scale (§5.2); the 60 VCA LEVEL is centre-zero with a −5…+5
  // scale (R-P5, C-4); the 106 HPF slider snaps to 4 positions.
  import { app } from '../model.svelte';
  import { Field } from '../protocol';
  import { fieldTooltip, fieldV, parseTyped } from './format';
  import { keyStep, startVerticalDrag, wheel, wheelStep, clamp } from './interaction';
  import { SLIDER_CAP_H, SLIDER_H, SLIDER_W, type HpfSliderDef, type SliderDef } from './layout';

  let { def, is106 }: { def: SliderDef | HpfSliderDef; is106: boolean } = $props();

  const isHpf = $derived(def.kind === 'hpfSlider');
  const field = $derived(def.kind === 'slider' ? def.field : Field.HPF);
  const max = $derived(isHpf ? 3 : 127);
  const raw = $derived(app.core?.edit[field] ?? 0);
  // a stored HPF outside 0–3 reads as 0 (§4.1)
  const value = $derived(isHpf ? (raw < 4 ? raw : 0) : raw);
  const label = $derived(def.kind === 'slider' ? (is106 && def.label106 ? def.label106 : def.label) : '');
  const ctx = $derived(app.core?.edit ?? []);
  const tooltip = () => fieldTooltip(def.aria, field, value, is106, ctx);
  const travel = SLIDER_H - SLIDER_CAP_H;
  const capY = $derived((1 - value / max) * travel);
  const ticks = $derived(Array.from({ length: isHpf ? 4 : 11 }, (_, i) => i));
  const tickY = (i: number, n: number) => (1 - i / (n - 1)) * travel + SLIDER_CAP_H / 2;
  // the same 11 tick positions, numbered −5…+5 with 0 at the centre (the panel's typographic minus)
  const centreZero = $derived(!is106 && field === Field.VCA_LEVEL);
  const tickLabel = (i: number) => {
    if (!centreZero) return String(i);
    const t = i - 5;
    return t > 0 ? `+${t}` : t < 0 ? `−${-t}` : '0';
  };

  let el: HTMLDivElement | undefined = $state();
  $effect(() => () => app.hideTip(def.id)); // a control that leaves the panel takes its tooltip

  function set(v: number) {
    const c = clamp(v, 0, max);
    if (c !== value) app.setField(field, c);
  }

  function showTip() {
    app.showTip({ owner: def.id, text: tooltip, x: def.x + SLIDER_W / 2, y: def.y + SLIDER_H + 6, top: def.y - 30 });
  }

  function onPointerDown(e: PointerEvent) {
    if (!el) return;
    startVerticalDrag(e, el, { get: () => value, set, min: 0, max }, () => app.scale, {
      pxPerStep: isHpf ? () => travel / 3 : undefined,
      begin: () => app.gesture({ f: field }, true),
      end: () => app.gesture({ f: field }, false),
    });
  }

  function onWheel(e: WheelEvent) {
    const d = wheelStep(e);
    if (!d) return;
    e.preventDefault();
    set(value + d);
  }

  function onKey(e: KeyboardEvent) {
    const v = keyStep(e, value, 0, max);
    if (v === null) return;
    e.preventDefault();
    set(v);
  }

  function onDblClick() {
    app.openTyped({
      owner: def.id,
      initial: fieldV(field, value, is106),
      x: def.x + SLIDER_W / 2 - 28,
      y: def.y - 24,
      w: 56,
      returnFocus: el ?? null,
      commit: (text) => {
        const b = parseTyped(isHpf ? 'hpf' : centreZero ? 'vca60' : 'byte', text, 0, max);
        if (b !== null) set(b);
      },
    });
  }
</script>

{#if label}
  <div class="legend" aria-hidden="true" style="left:{def.x + SLIDER_W / 2 - 30}px; top:{def.y - 30}px">{label}</div>
{/if}
<svg
  class="scale"
  aria-hidden="true"
  data-testid="scale-{def.id}"
  width="16"
  height={SLIDER_H}
  viewBox="0 0 16 {SLIDER_H}"
  style="left:{def.x - 16}px; top:{def.y}px"
>
  {#each ticks as i (i)}
    <text x="9" y={tickY(i, ticks.length) + 3.2} text-anchor="end" class="num">{tickLabel(i)}</text>
    <line x1="11" x2="15" y1={tickY(i, ticks.length)} y2={tickY(i, ticks.length)} class="tick" />
  {/each}
</svg>
<div
  bind:this={el}
  class="slider"
  role="slider"
  tabindex="0"
  aria-label={def.aria}
  aria-orientation="vertical"
  aria-valuemin="0"
  aria-valuemax={max}
  aria-valuenow={value}
  aria-valuetext={tooltip()}
  data-testid="ctl-{def.id}"
  data-value={value}
  style="left:{def.x}px; top:{def.y}px; width:{SLIDER_W}px; height:{SLIDER_H}px"
  onpointerdown={onPointerDown}
  ondblclick={onDblClick}
  onkeydown={onKey}
  onpointerenter={showTip}
  onpointerleave={() => app.hideTip(def.id)}
  onfocus={showTip}
  onblur={() => app.hideTip(def.id)}
  use:wheel={onWheel}
>
  <svg width={SLIDER_W} height={SLIDER_H} viewBox="0 0 {SLIDER_W} {SLIDER_H}" aria-hidden="true">
    <rect class="slot" x="7.5" y="2" width="5" height={SLIDER_H - 4} rx="2" />
    <g transform="translate(0 {capY})">
      <rect class="cap" x="0.5" y="0.5" width={SLIDER_W - 1} height={SLIDER_CAP_H - 1} rx="1.5" />
      <rect class="cap-band" x="1" y={SLIDER_CAP_H / 2 - 2} width={SLIDER_W - 2} height="4" />
      <rect class="cap-line" x="1" y={SLIDER_CAP_H / 2 - 1} width={SLIDER_W - 2} height="2" />
    </g>
  </svg>
</div>

<style>
  .legend {
    position: absolute;
    width: 60px;
    height: 26px;
    display: flex;
    align-items: flex-end;
    justify-content: center;
    text-align: center;
    white-space: pre-line;
    font-size: 11px;
    line-height: 11px;
    font-weight: 600;
    letter-spacing: 0.03em;
    color: var(--legend);
    pointer-events: none;
  }
  .scale {
    position: absolute;
    overflow: visible;
    pointer-events: none;
  }
  .num {
    font-family: 'Barlow Condensed', sans-serif;
    font-variant-numeric: tabular-nums;
    font-size: 9px;
    font-weight: 500;
    fill: var(--legend-dim);
  }
  .tick {
    stroke: var(--legend-dim);
    stroke-width: 1;
  }
  .slider {
    position: absolute;
    cursor: ns-resize;
    touch-action: none;
  }
  .slider svg {
    display: block;
  }
  .slot {
    fill: var(--slider-track);
    stroke: rgba(169, 164, 154, 0.35);
    stroke-width: 1;
  }
  .cap {
    fill: var(--slider-cap);
    stroke: #000;
    stroke-width: 1;
  }
  .cap-band {
    fill: var(--slider-track);
  }
  .cap-line {
    fill: #ffffff;
  }
</style>
