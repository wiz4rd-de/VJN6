<script lang="ts">
  // BANKS and SETTINGS in the panel's top strip (above the y = 40 rule, right-aligned to the right
  // edge box). The overlays were reachable only through the R-U2 shortcuts, so the mode choice in
  // Settings (§5.6, F-12) could not be found by clicking. Drawn in both modes; after the right edge
  // in the Tab order, so the R-U3 panel order is unchanged (DECISIONS.md).
  import { app } from '../model.svelte';

  const mac = /Mac|iPhone|iPad/.test(globalThis.navigator?.platform || globalThis.navigator?.userAgent || '');
  const keyName = (k: string) => (mac ? `⌥${k}` : `Alt+${k}`);

  const ITEMS = [
    { id: 'bank', label: 'BANKS', tip: 'BANK MANAGER', key: 'B', aria: 'Alt+B', x: 1232, w: 60 },
    { id: 'settings', label: 'SETTINGS', tip: 'SETTINGS', key: ',', aria: 'Alt+,', x: 1300, w: 80 },
  ] as const;
  const Y = 10;
  const H = 22;

  $effect(() => () => {
    for (const it of ITEMS) app.hideTip(`nav-${it.id}`);
  });
</script>

{#each ITEMS as it (it.id)}
  <button
    type="button"
    class="nav"
    aria-label={it.label}
    aria-haspopup="dialog"
    aria-keyshortcuts={it.aria}
    data-testid="nav-{it.id}"
    style="left:{it.x}px; top:{Y}px; width:{it.w}px; height:{H}px"
    onclick={() => app.openOverlay(it.id)}
    onpointerenter={() =>
      app.showTip({ owner: `nav-${it.id}`, text: () => `${it.tip} — ${keyName(it.key)}`, x: it.x + it.w / 2, y: Y + H + 6, top: Y })}
    onpointerleave={() => app.hideTip(`nav-${it.id}`)}
    onfocus={() =>
      app.showTip({ owner: `nav-${it.id}`, text: () => `${it.tip} — ${keyName(it.key)}`, x: it.x + it.w / 2, y: Y + H + 6, top: Y })}
    onblur={() => app.hideTip(`nav-${it.id}`)}>{it.label}</button
  >
{/each}

<style>
  .nav {
    position: absolute;
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    border: 1px solid var(--legend-dim);
    border-radius: 3px;
    background: transparent;
    color: var(--legend);
    font-family: 'Barlow Condensed', sans-serif;
    font-size: 12px;
    font-weight: 600;
    line-height: 20px;
    letter-spacing: 0.12em;
    cursor: pointer;
  }
  .nav:hover {
    border-color: var(--legend);
    background: rgba(255, 255, 255, 0.06);
  }
  .nav:active {
    background: rgba(255, 255, 255, 0.12);
  }
</style>
