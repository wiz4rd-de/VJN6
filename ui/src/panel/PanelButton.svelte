<script lang="ts">
  // A 26 × 26 panel button with its LED (§5.2). Click = press; Shift+click (Shift+Enter) = the
  // combination press (R-U4). LFO TRIG is momentary: held from pointer-down until pointer-up
  // anywhere, window blur or editor close; with keyboard focus, held while Enter is held.
  import { app } from '../model.svelte';
  import { Led } from '../protocol';
  import { keyTransposeV } from './format';
  import { BUTTON, inMode, type ButtonDef } from './layout';
  import { CAP_FILL } from '../skins/skins';

  let { def, is106 }: { def: ButtonDef; is106: boolean } = $props();

  const lit = $derived(app.ledOn(def.led));
  const cap = $derived((is106 ? def.cap106 : def.cap60) ?? 'white');
  const blinking = $derived(!!app.core?.blinking?.includes(def.led));
  const tooltip = () => {
    if (def.led === Led.KEY_TRANSPOSE) return `${def.aria} ${keyTransposeV(app.core?.perf[13] ?? 0)}`;
    const combo = def.combo && inMode(def.comboModes ?? 'both', is106) ? def.combo : '';
    return combo ? `${def.aria} — ${combo}` : def.aria;
  };

  let el: HTMLButtonElement | undefined = $state();
  let held = false;

  function showTip() {
    const below = def.y + BUTTON + (def.label ? 18 : 6);
    app.showTip({ owner: def.id, text: tooltip, x: def.x + BUTTON / 2, y: below, top: def.y - 12 });
  }

  function hold() {
    if (held) return;
    held = true;
    app.press(def.b);
    const releaseOn = () => unhold();
    const onKeyUp = (e: KeyboardEvent) => {
      if (e.key === 'Enter') unhold();
    };
    const cleanup = () => {
      window.removeEventListener('pointerup', releaseOn);
      window.removeEventListener('pointercancel', releaseOn);
      window.removeEventListener('blur', releaseOn);
      window.removeEventListener('pagehide', releaseOn);
      window.removeEventListener('keyup', onKeyUp);
    };
    window.addEventListener('pointerup', releaseOn);
    window.addEventListener('pointercancel', releaseOn);
    window.addEventListener('blur', releaseOn);
    window.addEventListener('pagehide', releaseOn);
    window.addEventListener('keyup', onKeyUp);
    unholdCleanup = cleanup;
  }

  let unholdCleanup: (() => void) | null = null;

  function unhold() {
    if (!held) return;
    held = false;
    unholdCleanup?.();
    unholdCleanup = null;
    app.release(def.b);
  }

  $effect(() => () => {
    unhold(); // editor close / unmount while held
    app.hideTip(def.id);
  });

  function onPointerDown(e: PointerEvent) {
    if (!def.momentary || e.button !== 0) return;
    e.preventDefault();
    el?.focus({ preventScroll: true });
    hold();
  }

  function onClick(e: MouseEvent) {
    if (def.momentary) return;
    app.press(def.b, def.i, e.shiftKey);
  }

  function onKey(e: KeyboardEvent) {
    if (e.key !== 'Enter' || e.altKey || e.ctrlKey || e.metaKey) return;
    e.preventDefault(); // Enter is handled here (not as a native click) so Shift reaches the press
    if (e.repeat) return;
    if (def.momentary) hold();
    else app.press(def.b, def.i, e.shiftKey);
  }
</script>

<span
  class="led"
  class:on={lit}
  aria-hidden="true"
  style="left:{def.x + BUTTON / 2 - 3.5}px; top:{def.y - 10}px"
></span>
<button
  bind:this={el}
  type="button"
  class="cap"
  aria-label={def.aria}
  aria-pressed={lit}
  data-testid="ctl-{def.id}"
  data-lit={lit}
  data-blinking={blinking}
  style="left:{def.x}px; top:{def.y}px; width:{BUTTON}px; height:{BUTTON}px; --cap:{CAP_FILL[cap]}"
  onpointerdown={onPointerDown}
  onclick={onClick}
  onkeydown={onKey}
  onpointerenter={showTip}
  onpointerleave={() => app.hideTip(def.id)}
  onfocus={showTip}
  onblur={() => {
    app.hideTip(def.id);
    if (held) unhold();
  }}
></button>
{#if def.label}
  <span class="label" aria-hidden="true" style="left:{def.x + BUTTON / 2 - 40}px; top:{def.y + BUTTON + 3}px"
    >{def.label}</span
  >
{/if}

<style>
  .led {
    position: absolute;
    width: 7px;
    height: 7px;
    border-radius: 50%;
    background: var(--led-off);
    box-shadow: inset 0 0 0 1px rgba(0, 0, 0, 0.6);
    pointer-events: none;
  }
  .led.on {
    background: var(--lamp-on);
    box-shadow:
      0 0 4px 1px rgba(255, 90, 72, 0.7),
      inset 0 0 0 1px rgba(0, 0, 0, 0.3);
  }
  .cap {
    position: absolute;
    margin: 0;
    padding: 0;
    border: 1px solid #000;
    border-radius: 2px;
    background: var(--cap);
    box-shadow:
      inset 0 -3px 0 rgba(0, 0, 0, 0.18),
      inset 0 1px 0 rgba(255, 255, 255, 0.35);
    cursor: pointer;
  }
  .cap:active {
    box-shadow:
      inset 0 2px 0 rgba(0, 0, 0, 0.25),
      inset 0 -1px 0 rgba(255, 255, 255, 0.2);
  }
  .label {
    position: absolute;
    width: 80px;
    text-align: center;
    font-size: 10px;
    line-height: 11px;
    font-weight: 600;
    letter-spacing: 0.03em;
    color: var(--legend);
    white-space: nowrap;
    pointer-events: none;
  }
</style>
