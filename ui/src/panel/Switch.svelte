<script lang="ts">
  // A vertical slide switch with n positions (§5.2). Click a position (R-U1); Up/Down move one
  // position (R-U1a). Tooltip: the legend (R-P5); the 60 HPF switch also names its corner.
  import { app } from '../model.svelte';
  import { effectiveSwitch, Field } from '../protocol';
  import { fieldTooltip } from './format';
  import { SWITCH_STEP, SWITCH_W, type SwitchDef } from './layout';

  let { def, is106 }: { def: SwitchDef; is106: boolean } = $props();

  const positions = $derived(is106 && def.positions106 ? def.positions106 : def.positions);
  const isField = $derived('field' in def.target);
  const value = $derived.by(() => {
    const c = app.core;
    if (!c) return 0;
    if ('field' in def.target) return effectiveSwitch(def.target.field, c.edit[def.target.field] ?? 0, is106);
    return c.perf[def.target.perf] ?? 0;
  });
  const index = $derived(Math.max(0, positions.findIndex((p) => p.value === value)));
  const h = $derived(positions.length * SWITCH_STEP);
  const values = $derived(positions.map((p) => p.value));
  const tooltip = () =>
    'field' in def.target && def.target.field === Field.HPF
      ? fieldTooltip(def.aria, Field.HPF, value, is106, app.core?.edit ?? [])
      : def.aria;

  let el: HTMLDivElement | undefined = $state();
  $effect(() => () => app.hideTip(def.id)); // a control that leaves the panel takes its tooltip

  function select(v: number) {
    if ('field' in def.target) app.setField(def.target.field, v);
    else app.setPerf(def.target.perf, v);
  }

  function showTip() {
    const below = def.labels === 'ends' ? 16 : 6;
    const legendBelow = def.legend && def.legendAt === 'below' ? 26 : 0;
    app.showTip({ owner: def.id, text: tooltip, x: def.x + SWITCH_W / 2, y: def.y + h + below + legendBelow, top: def.y - 30 });
  }

  function onBodyDown(e: PointerEvent) {
    if (e.button !== 0) return;
    e.preventDefault();
    el?.focus({ preventScroll: true });
    const rect = (e.currentTarget as HTMLElement).getBoundingClientRect();
    const y = (e.clientY - rect.top) / Math.max(app.scale, 0.01);
    const i = Math.min(positions.length - 1, Math.max(0, Math.floor(y / SWITCH_STEP)));
    select(positions[i].value);
  }

  function onLabelDown(e: PointerEvent, v: number) {
    if (e.button !== 0) return;
    e.preventDefault();
    el?.focus({ preventScroll: true });
    select(v);
  }

  function onKey(e: KeyboardEvent) {
    if (e.altKey || e.ctrlKey || e.metaKey) return;
    if (e.key === 'ArrowUp' && index > 0) {
      e.preventDefault();
      select(positions[index - 1].value);
    } else if (e.key === 'ArrowDown' && index < positions.length - 1) {
      e.preventDefault();
      select(positions[index + 1].value);
    } else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
      e.preventDefault();
    }
  }
</script>

<div
  bind:this={el}
  class="switch"
  role="slider"
  tabindex="0"
  aria-label={def.aria}
  aria-orientation="vertical"
  aria-valuemin={Math.min(...values)}
  aria-valuemax={Math.max(...values)}
  aria-valuenow={value}
  aria-valuetext={positions[index]?.label}
  data-testid="ctl-{def.id}"
  data-value={value}
  data-field={isField ? 'field' : 'perf'}
  style="left:{def.x}px; top:{def.y}px; width:{SWITCH_W}px; height:{h}px"
  onkeydown={onKey}
  onpointerenter={showTip}
  onpointerleave={() => app.hideTip(def.id)}
  onfocus={showTip}
  onblur={() => app.hideTip(def.id)}
>
  <svg
    class="body"
    width={SWITCH_W}
    height={h}
    viewBox="0 0 {SWITCH_W} {h}"
    aria-hidden="true"
    onpointerdown={onBodyDown}
  >
    <rect class="slot" x="0.5" y="0.5" width={SWITCH_W - 1} height={h - 1} rx="2" />
    <rect class="nub" x="2" y={index * SWITCH_STEP + 2} width={SWITCH_W - 4} height={SWITCH_STEP - 4} rx="1" />
    <rect class="nub-line" x="2" y={index * SWITCH_STEP + SWITCH_STEP / 2 - 1} width={SWITCH_W - 4} height="2" />
  </svg>
  {#each positions as p, i (p.value)}
    {#if def.labels === 'side'}
      <span
        class="pos side"
        class:active={i === index}
        aria-hidden="true"
        data-testid="ctl-{def.id}-pos-{p.value}"
        style="top:{i * SWITCH_STEP}px"
        onpointerdown={(e) => onLabelDown(e, p.value)}>{p.label}</span
      >
    {:else}
      <span
        class="pos end"
        class:active={i === index}
        aria-hidden="true"
        data-testid="ctl-{def.id}-pos-{p.value}"
        style={i === 0 ? 'top:-15px' : `top:${h + 2}px`}
        onpointerdown={(e) => onLabelDown(e, p.value)}>{p.label}</span
      >
    {/if}
  {/each}
  {#if def.legend}
    <span
      class="legend"
      aria-hidden="true"
      style={def.legendAt === 'below'
        ? `top:${h + 4}px`
        : `bottom:${h + (def.labels === 'ends' ? 17 : 4)}px`}>{def.legend}</span
    >
  {/if}
</div>

<style>
  .switch {
    position: absolute;
    cursor: pointer;
  }
  .body {
    display: block;
    position: absolute;
    left: 0;
    top: 0;
  }
  .slot {
    fill: var(--slider-track);
    stroke: rgba(169, 164, 154, 0.55);
    stroke-width: 1;
  }
  .nub {
    fill: var(--slider-cap);
  }
  .nub-line {
    fill: #ffffff;
  }
  .pos {
    position: absolute;
    font-size: 10px;
    line-height: 16px;
    font-weight: 600;
    color: var(--legend-dim);
    white-space: nowrap;
    cursor: pointer;
  }
  .pos.active {
    color: var(--legend);
  }
  .side {
    left: 16px;
  }
  .end {
    left: 50%;
    transform: translateX(-50%);
    line-height: 13px;
  }
  .legend {
    position: absolute;
    left: 50%;
    transform: translateX(-50%);
    font-size: 11px;
    line-height: 11px;
    font-weight: 600;
    letter-spacing: 0.03em;
    color: var(--legend);
    white-space: pre;
    text-align: center;
    pointer-events: none;
  }
</style>
