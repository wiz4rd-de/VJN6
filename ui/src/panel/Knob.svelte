<script lang="ts">
  // A 44 px rotary knob for a performance setting (§5.2); dragged vertically like a slider (R-U1).
  import { app } from '../model.svelte';
  import { Perf } from '../protocol';
  import { parseTyped, perfTooltip, perfV } from './format';
  import { clamp, keyStep, startVerticalDrag, wheel, wheelStep } from './interaction';
  import { KNOB, type KnobDef } from './layout';
  import { skinFor } from '../skins/skins';

  let { def, is106 }: { def: KnobDef; is106: boolean } = $props();

  const min = 0;
  const max = 127;
  const value = $derived(app.core?.perf[def.perf] ?? 0);
  const tooltip = () => perfTooltip(def.aria, def.perf, value, is106);
  const skin = $derived(skinFor(is106));
  const angle = $derived(-135 + (270 * (value - min)) / (max - min));
  const r = KNOB / 2;

  let el: HTMLDivElement | undefined = $state();
  $effect(() => () => app.hideTip(def.id)); // a control that leaves the panel takes its tooltip

  function set(v: number) {
    const c = clamp(v, min, max);
    if (c !== value) app.setPerf(def.perf, c);
  }

  function showTip() {
    app.showTip({ owner: def.id, text: tooltip, x: def.x + r, y: def.y + KNOB + 18, top: def.y });
  }

  function onPointerDown(e: PointerEvent) {
    if (!el) return;
    startVerticalDrag(e, el, { get: () => value, set, min, max }, () => app.scale, {
      begin: () => app.gesture({ p: def.perf }, true),
      end: () => app.gesture({ p: def.perf }, false),
    });
  }

  function onWheel(e: WheelEvent) {
    const d = wheelStep(e);
    if (!d) return;
    e.preventDefault();
    set(value + d);
  }

  function onKey(e: KeyboardEvent) {
    const v = keyStep(e, value, min, max);
    if (v === null) return;
    e.preventDefault();
    set(v);
  }

  function onDblClick() {
    const tune = def.perf === Perf.TUNE;
    app.openTyped({
      owner: def.id,
      initial: perfV(def.perf, value),
      x: def.x + r - 28,
      y: def.y + r - 10,
      w: 56,
      returnFocus: el ?? null,
      commit: (text) => {
        const b = parseTyped(tune ? 'tune' : 'byte', text, min, max);
        if (b !== null) set(b);
      },
    });
  }
</script>

<div
  bind:this={el}
  class="knob"
  role="slider"
  tabindex="0"
  aria-label={def.aria}
  aria-valuemin={min}
  aria-valuemax={max}
  aria-valuenow={value}
  aria-valuetext={tooltip()}
  data-testid="ctl-{def.id}"
  data-value={value}
  style="left:{def.x}px; top:{def.y}px; width:{KNOB}px; height:{KNOB}px"
  onpointerdown={onPointerDown}
  ondblclick={onDblClick}
  onkeydown={onKey}
  onpointerenter={showTip}
  onpointerleave={() => app.hideTip(def.id)}
  onfocus={showTip}
  onblur={() => app.hideTip(def.id)}
  use:wheel={onWheel}
>
  <svg width={KNOB} height={KNOB} viewBox="0 0 {KNOB} {KNOB}" aria-hidden="true">
    {#each [-135, -81, -27, 27, 81, 135] as a (a)}
      <line
        class="dot"
        x1={r}
        y1="1"
        x2={r}
        y2="4"
        transform="rotate({a} {r} {r})"
      />
    {/each}
    <circle cx={r} cy={r} r={r - 5} fill={skin.knob} stroke="#000" stroke-width="1" />
    <circle cx={r} cy={r} r={r - 9} fill="none" stroke="rgba(236,232,220,0.12)" stroke-width="1" />
    <g transform="rotate({angle} {r} {r})">
      <rect x={r - 1.25} y="7" width="2.5" height="13" rx="1" fill={skin.knobPointer} />
    </g>
  </svg>
</div>
<div class="label" aria-hidden="true" style="left:{def.x + r - 40}px; top:{def.y + KNOB + 2}px">{def.label}</div>

<style>
  .knob {
    position: absolute;
    cursor: ns-resize;
    touch-action: none;
    border-radius: 50%;
  }
  .knob svg {
    display: block;
    overflow: visible;
  }
  .dot {
    stroke: var(--legend-dim);
    stroke-width: 1.2;
  }
  .label {
    position: absolute;
    width: 80px;
    text-align: center;
    font-size: 11px;
    line-height: 12px;
    font-weight: 600;
    letter-spacing: 0.03em;
    color: var(--legend);
    pointer-events: none;
    white-space: nowrap;
  }
</style>
