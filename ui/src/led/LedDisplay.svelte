<script lang="ts">
  // The two-digit LED (§5.3) with its LED-message tooltip (§5.4) and the slot tooltip.
  import { app } from '../model.svelte';
  import { LED_MESSAGES, ledTooltip } from '../protocol';
  import { decimalPoint, DIGIT_X, DIGIT_Y, glyph, LED_H, LED_W, SEGMENTS, segmentPoints } from './segments';

  let { x, y }: { x: number; y: number } = $props();

  const digits = [segmentPoints(DIGIT_X[0], DIGIT_Y), segmentPoints(DIGIT_X[1], DIGIT_Y)] as const;
  const dps = [decimalPoint(0), decimalPoint(1)] as const;

  const led = $derived(app.core?.led);
  const lit = $derived(app.started && !!led);
  const message = $derived(lit && led ? led.message : 0);
  // WRITE blink: the digits blink 400 ms on / 400 ms off; an LED message is never blinked
  const blinkOff = $derived(lit && !!led?.blink && message === 0 && !app.blinkOn);
  const text = $derived(!lit || !led ? '  ' : blinkOff ? '  ' : (led.text ?? '  ').padEnd(2, ' ').slice(0, 2));
  const dp = $derived(lit && !!led?.dp && message === 0);
  const on = $derived([new Set(glyph(text[0])), new Set(glyph(text[1]))]);
  const msgTip = $derived(message ? (LED_MESSAGES[message]?.tooltip ?? '') : '');
  const slotTip = () => (app.core ? ledTooltip(app.core) : '');

  function showTip() {
    app.showTip({ owner: 'led', text: slotTip, x: x + LED_W / 2, y: y + LED_H + (msgTip ? 30 : 8), top: y });
  }
</script>

<div
  class="led"
  role="img"
  aria-label={app.core ? `LED ${text.trim() || 'blank'}, ${slotTip()}` : 'LED'}
  data-testid="led"
  data-text={text}
  data-raw-text={led?.text ?? ''}
  data-dp={dp}
  data-blink={lit && !!led?.blink}
  data-message={message}
  data-slot-tooltip={slotTip()}
  style="left:{x}px; top:{y}px; width:{LED_W}px; height:{LED_H}px"
  onpointerenter={showTip}
  onpointerleave={() => app.hideTip('led')}
>
  <svg width={LED_W} height={LED_H} viewBox="0 0 {LED_W} {LED_H}" aria-hidden="true">
    <rect class="window" x="0.5" y="0.5" width={LED_W - 1} height={LED_H - 1} rx="3" />
    {#each [0, 1] as d (d)}
      {#each SEGMENTS as s (s)}
        <polygon points={digits[d][s]} class:on={on[d].has(s)} class="seg" />
      {/each}
      <circle cx={dps[d].cx} cy={dps[d].cy} r={dps[d].r} class:on={dp} class="seg" />
    {/each}
  </svg>
</div>
{#if msgTip}
  <div class="msg" role="status" data-testid="led-message" style="left:{x + LED_W / 2}px; top:{y + LED_H + 2}px">
    {msgTip}
  </div>
{/if}

<style>
  .led {
    position: absolute;
  }
  .led svg {
    display: block;
  }
  .window {
    fill: #120707;
    stroke: #000;
    stroke-width: 1;
  }
  .seg {
    fill: var(--led-off);
  }
  .seg.on {
    fill: var(--led-on);
    filter: drop-shadow(0 0 2px rgba(255, 59, 47, 0.75));
  }
  .msg {
    position: absolute;
    transform: translateX(-50%);
    z-index: 55;
    background: var(--tooltip);
    color: var(--legend);
    font-size: 12px;
    line-height: 15px;
    font-weight: 600;
    letter-spacing: 0.04em;
    padding: 3px 7px;
    border-radius: 3px;
    white-space: nowrap;
    pointer-events: none;
  }
</style>
