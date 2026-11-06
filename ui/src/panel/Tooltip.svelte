<script lang="ts">
  import { app } from '../model.svelte';
  import { CANVAS_H, CANVAS_W } from './layout';

  const HEIGHT = 26; // one line of tooltip text with its padding

  const tip = $derived(app.tip);
  const text = $derived(tip ? tip.text() : '');
  // keep the box inside the canvas: half of a wide tooltip is about 110 px
  const left = $derived(tip ? Math.min(Math.max(tip.x, 110), CANVAS_W - 110) : 0);
  // below the control, or above it when the canvas ends first (memory row)
  const top = $derived(tip ? (tip.y + HEIGHT <= CANVAS_H ? tip.y : tip.top - HEIGHT - 4) : 0);
</script>

{#if tip && text}
  <div class="tooltip tabular" role="tooltip" data-testid="tooltip" style="left:{left}px; top:{top}px">
    {text}
  </div>
{/if}

<style>
  .tooltip {
    position: absolute;
    transform: translateX(-50%);
    z-index: 60;
    background: var(--tooltip);
    color: var(--legend);
    font-size: 13px;
    line-height: 16px;
    font-weight: 500;
    letter-spacing: 0.02em;
    padding: 4px 8px;
    border-radius: 3px;
    border: 1px solid #000;
    white-space: nowrap;
    pointer-events: none;
    box-shadow: 0 2px 6px rgba(0, 0, 0, 0.5);
  }
</style>
