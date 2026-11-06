<script lang="ts">
  // Canvas and scaling (§5.1, §4.4 Zoom): the 1400 × 560 layout scaled uniformly, clamped to
  // 50–200 %, letterboxed in --body. Web: a zoom percentage caps the fit scale and the page scrolls
  // below 50 %. Desktop: the host sizes the window; the canvas fits it.
  import { app } from './model.svelte';
  import BankManager from './bank/BankManager.svelte';
  import { CANVAS_H, CANVAS_W } from './panel/layout';
  import Panel from './panel/Panel.svelte';
  import Tooltip from './panel/Tooltip.svelte';
  import TypedEntry from './panel/TypedEntry.svelte';
  import { Setting } from './protocol';
  import Settings from './settings/Settings.svelte';

  let wrap: HTMLDivElement | undefined = $state();
  let availW = $state(CANVAS_W);
  let availH = $state(CANVAS_H);

  $effect(() => {
    if (!wrap) return;
    const ro = new ResizeObserver(() => {
      if (!wrap) return;
      availW = wrap.clientWidth;
      availH = wrap.clientHeight;
    });
    ro.observe(wrap);
    return () => ro.disconnect();
  });

  const scale = $derived.by(() => {
    const fit = Math.min(availW / CANVAS_W, availH / CANVAS_H);
    let s = fit;
    const zoom = app.core?.settings[Setting.ZOOM] ?? 0;
    if (!app.isDesktop && zoom >= 50) s = Math.min(fit, zoom / 100);
    return Math.min(2, Math.max(0.5, s));
  });

  $effect(() => {
    app.scale = scale;
  });

  const overlayOpen = $derived(app.overlay !== 'none');
</script>

<div class="wrap" bind:this={wrap} data-testid="stage-wrap">
  <div class="frame" style="width:{CANVAS_W * scale}px; height:{CANVAS_H * scale}px">
    <div
      class="stage"
      data-testid="stage"
      data-scale={scale}
      style="transform: scale({scale})"
      inert={overlayOpen || !app.started}
    >
      <Panel />
      <Tooltip />
      <TypedEntry />
    </div>
    {#if overlayOpen}
      <div class="backdrop" data-testid="overlay-backdrop" role="presentation" onclick={() => app.closeOverlay()}></div>
      <div class="overlay-layer" style="transform: scale({scale})">
        {#if app.overlay === 'bank'}
          <BankManager />
        {:else if app.overlay === 'settings'}
          <Settings />
        {/if}
      </div>
    {/if}
  </div>
</div>

<style>
  .wrap {
    flex: 1;
    min-height: 0;
    display: flex;
    overflow: auto;
    background: var(--body);
  }
  .frame {
    position: relative;
    margin: auto;
    flex: none;
  }
  .stage,
  .overlay-layer {
    position: absolute;
    left: 0;
    top: 0;
    width: 1400px;
    height: 560px;
    transform-origin: 0 0;
  }
  .overlay-layer {
    z-index: 90;
    pointer-events: none;
  }
  .backdrop {
    position: fixed;
    inset: 0;
    z-index: 80;
    background: rgba(0, 0, 0, 0.45);
  }
</style>
