<script lang="ts">
  import { app } from './model.svelte';
  import Stage from './Stage.svelte';
  import StartCard from './web/StartCard.svelte';

  const SLOT_DRAG = 'application/x-vjn6-slot';

  function hasFiles(e: DragEvent): boolean {
    const types = e.dataTransfer ? Array.from(e.dataTransfer.types) : [];
    return types.includes('Files') && !types.includes(SLOT_DRAG);
  }

  // Files dropped anywhere on the window: only the first is used; the bank manager opens (F-10).
  function onDragOver(e: DragEvent) {
    if (!hasFiles(e) || !app.core || !app.started) return;
    e.preventDefault();
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy';
  }

  function onDrop(e: DragEvent) {
    if (!hasFiles(e)) return;
    e.preventDefault();
    if (!app.core || !app.started) return;
    app.openOverlay('bank');
    void app.loadFiles(e.dataTransfer?.files);
  }
</script>

<svelte:window ondragover={onDragOver} ondrop={onDrop} />

{#if app.fatal}
  <div class="fatal" role="alert" data-testid="fatal-message">
    <div class="fatal-box">
      <div class="fatal-title">VJN6</div>
      <p>{app.fatal}</p>
    </div>
  </div>
{:else}
  <div class="app">
    {#if app.storageBanner}
      <div class="banner" role="status" data-testid="banner-storage">Changes will not be saved in this browser.</div>
    {/if}
    {#if app.audioBanner}
      <div class="banner" role="status" data-testid="banner-audio">No audio output available.</div>
    {/if}
    {#if app.core}
      <Stage />
      {#if !app.started}
        <StartCard />
      {/if}
    {/if}
  </div>
{/if}

<style>
  .app {
    height: 100%;
    display: flex;
    flex-direction: column;
  }
  .banner {
    flex: none;
    padding: 6px 16px;
    font-size: 15px;
    font-weight: 600;
    color: var(--legend);
    background: var(--overlay);
    border-bottom: 1px solid rgba(169, 164, 154, 0.4);
    text-align: center;
  }
  .fatal {
    height: 100%;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 24px;
    box-sizing: border-box;
  }
  .fatal-box {
    max-width: 640px;
    background: var(--overlay);
    border: 1px solid rgba(169, 164, 154, 0.5);
    border-radius: 8px;
    padding: 24px 32px;
    font-size: 18px;
    line-height: 26px;
    user-select: text;
    -webkit-user-select: text;
  }
  .fatal-title {
    font-family: 'Oswald', sans-serif;
    font-size: 28px;
    margin-bottom: 8px;
  }
  .fatal-box p {
    margin: 0;
  }
</style>
