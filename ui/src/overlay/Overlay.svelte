<script lang="ts">
  // Modal overlay frame (§5.5, §5.6): 960 × 520 logical px, centred on the canvas and scaled with
  // it. Esc, Close or a click outside closes it (the backdrop lives in the Stage).
  import type { Snippet } from 'svelte';
  import { OVERLAY_H, OVERLAY_W, CANVAS_H, CANVAS_W } from '../panel/layout';

  let { title, testid, children }: { title: string; testid: string; children: Snippet } = $props();

  let el: HTMLDivElement | undefined = $state();

  $effect(() => {
    const previous = document.activeElement as HTMLElement | null;
    const first = el?.querySelector<HTMLElement>(
      'button:not([aria-disabled="true"]), [tabindex="0"], input, select',
    );
    (first ?? el)?.focus({ preventScroll: true });
    return () => {
      if (previous && document.contains(previous)) previous.focus({ preventScroll: true });
    };
  });

  /** Keeps Tab inside the dialog (reading order). */
  function onKey(e: KeyboardEvent) {
    if (e.key !== 'Tab' || !el) return;
    const items = [
      ...el.querySelectorAll<HTMLElement>('button, [tabindex="0"], input, select'),
    ].filter((n) => !n.hasAttribute('disabled') && n.offsetParent !== null && n.tabIndex >= 0);
    if (!items.length) return;
    const first = items[0];
    const last = items[items.length - 1];
    if (e.shiftKey && document.activeElement === first) {
      e.preventDefault();
      last.focus();
    } else if (!e.shiftKey && document.activeElement === last) {
      e.preventDefault();
      first.focus();
    }
  }
</script>

<div
  bind:this={el}
  class="overlay"
  role="dialog"
  aria-modal="true"
  aria-label={title}
  tabindex="-1"
  data-testid={testid}
  style="left:{(CANVAS_W - OVERLAY_W) / 2}px; top:{(CANVAS_H - OVERLAY_H) / 2}px; width:{OVERLAY_W}px; height:{OVERLAY_H}px"
  onkeydown={onKey}
>
  {@render children()}
</div>

<style>
  .overlay {
    position: absolute;
    box-sizing: border-box;
    background: var(--overlay);
    color: var(--legend);
    border: 1px solid rgba(169, 164, 154, 0.5);
    border-radius: 6px;
    box-shadow: 0 10px 40px rgba(0, 0, 0, 0.6);
    pointer-events: auto;
    font-size: 15px;
    line-height: 20px;
    overflow: hidden;
    user-select: none;
  }
</style>
