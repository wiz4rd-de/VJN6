<script lang="ts">
  // The front panel (§5.2, §11): 1400 × 560 logical px, both skins on one geometry.
  import { app } from '../model.svelte';
  import LedDisplay from '../led/LedDisplay.svelte';
  import { headerText, skinFor } from '../skins/skins';
  import Knob from './Knob.svelte';
  import { BOXES, CANVAS_H, CANVAS_W, CONTROLS, HEADER_H, inMode, TEXTS } from './layout';
  import HeaderButtons from './HeaderButtons.svelte';
  import Lever from './Lever.svelte';
  import PanelButton from './PanelButton.svelte';
  import Slider from './Slider.svelte';
  import Switch from './Switch.svelte';

  const is106 = $derived(app.is106);
  const skin = $derived(skinFor(is106));
  const controls = $derived(CONTROLS.filter((c) => inMode(c.modes, is106)));
  const texts = $derived(TEXTS.filter((t) => inMode(t.modes, is106)));

  // flat vector wood grain for the 60's end cheeks (§11)
  const grain = Array.from({ length: 9 }, (_, i) => {
    const x = 2 + i * 1.2;
    const w = 0.6 + (i % 3) * 0.25;
    let d = `M ${x} 0`;
    for (let y = 40; y <= CANVAS_H; y += 40) {
      const dx = Math.sin((y + i * 37) / 53) * 1.1;
      d += ` Q ${x + dx} ${y - 20} ${x + dx * 0.4} ${y}`;
    }
    return { d, w };
  });
</script>

<div
  class="panel"
  data-testid="panel"
  data-mode={is106 ? '106' : '60'}
  style="width:{CANVAS_W}px; height:{CANVAS_H}px; background:{skin.panel}"
>
  <svg class="decor" width={CANVAS_W} height={CANVAS_H} viewBox="0 0 {CANVAS_W} {CANVAS_H}" aria-hidden="true">
    {#each [0, CANVAS_W - 12] as cx (cx)}
      {#if skin.cheek === 'wood'}
        <rect x={cx} y="0" width="12" height={CANVAS_H} fill="var(--wood)" />
        <g transform="translate({cx} 0)">
          {#each grain as g, gi (gi)}
            <path d={g.d} fill="none" stroke="rgba(0,0,0,0.28)" stroke-width={g.w} />
          {/each}
        </g>
      {:else}
        <rect x={cx} y="0" width="12" height={CANVAS_H} fill="#0B0B0C" />
      {/if}
    {/each}
    <line x1="20" x2={CANVAS_W - 20} y1="40" y2="40" stroke={skin.dim} stroke-opacity="0.35" stroke-width="1" />
    {#each BOXES as b (b.id)}
      <rect
        x={b.x + 0.75}
        y={b.y + 0.75}
        width={b.w - 1.5}
        height={b.h - 1.5}
        rx="3"
        fill="none"
        stroke={skin.border[b.id] ?? skin.dim}
        stroke-width="1.5"
      />
      {#if b.title && skin.header[b.id]}
        <rect x={b.x + 1.5} y={b.y + 1.5} width={b.w - 3} height={HEADER_H - 1.5} rx="2" fill={skin.header[b.id]} />
        <text
          x={b.x + b.w / 2}
          y={b.y + 16}
          text-anchor="middle"
          class="title"
          fill={headerText(skin.header[b.id])}>{b.title}</text
        >
      {/if}
    {/each}
  </svg>

  {#each texts as t (t.id)}
    {#if t.kind === 'wordmark'}
      <div class="wordmark" style="left:{t.x - 26}px; top:{t.y}px" aria-hidden="true">{t.text}</div>
    {:else}
      <div
        class="group"
        class:center={t.align === 'center'}
        style="left:{t.align === 'center' ? t.x - 50 : t.x}px; top:{t.y}px"
        aria-hidden="true"
      >
        {t.text}
      </div>
    {/if}
  {/each}
  <div class="badge" data-testid="mode-badge" style="left:{1334}px; top:{500}px">{is106 ? '106' : '60'}</div>

  {#each controls as c (c.kind + ':' + c.id)}
    {#if c.kind === 'slider' || c.kind === 'hpfSlider'}
      <Slider def={c} {is106} />
    {:else if c.kind === 'knob'}
      <Knob def={c} {is106} />
    {:else if c.kind === 'switch'}
      <Switch def={c} {is106} />
    {:else if c.kind === 'button'}
      <PanelButton def={c} {is106} />
    {:else if c.kind === 'lever'}
      <Lever def={c} {is106} />
    {:else if c.kind === 'led'}
      <LedDisplay x={c.x} y={c.y} />
    {/if}
  {/each}
  <HeaderButtons />
</div>

<style>
  .panel {
    position: relative;
    color: var(--legend);
  }
  .decor {
    position: absolute;
    left: 0;
    top: 0;
    pointer-events: none;
  }
  .title {
    font-family: 'Barlow Condensed', sans-serif;
    font-size: 13px;
    font-weight: 600;
    letter-spacing: 0.12em;
  }
  .group {
    position: absolute;
    font-size: 10px;
    line-height: 12px;
    font-weight: 600;
    letter-spacing: 0.08em;
    color: var(--legend-dim);
    white-space: nowrap;
    pointer-events: none;
  }
  .group.center {
    width: 100px;
    text-align: center;
  }
  .wordmark {
    position: absolute;
    width: 52px;
    text-align: center;
    font-family: 'Oswald', sans-serif;
    font-weight: 500;
    font-size: 21px;
    line-height: 26px;
    letter-spacing: 0.04em;
    color: var(--legend);
    pointer-events: none;
  }
  .badge {
    position: absolute;
    width: 40px;
    height: 22px;
    box-sizing: border-box;
    border: 1.5px solid var(--legend);
    border-radius: 4px;
    text-align: center;
    font-family: 'Oswald', sans-serif;
    font-weight: 500;
    font-size: 14px;
    line-height: 19px;
    color: var(--legend);
    pointer-events: none;
  }
</style>
