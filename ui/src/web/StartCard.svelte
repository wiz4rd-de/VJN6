<script lang="ts">
  // Web start card (§5.7): the click or key that dismisses it is not passed on to any control or
  // to the computer keyboard; then audio starts and Web MIDI is requested. The key part lives in
  // app.boot() so that it is active before the card is drawn.
  import { app } from '../model.svelte';

  function onClick(e: MouseEvent) {
    e.preventDefault();
    e.stopPropagation();
    app.startWeb();
  }
</script>

<div
  class="start"
  role="button"
  tabindex="-1"
  data-testid="start-card"
  aria-label="Click or press any key to start VJN6"
  onclick={onClick}
  onpointerdown={(e) => e.stopPropagation()}
  onkeydown={() => {}}
>
  <div class="card">Click or press any key to start VJN6</div>
</div>

<style>
  .start {
    position: fixed;
    inset: 0;
    z-index: 100;
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(0, 0, 0, 0.35);
    cursor: pointer;
  }
  .card {
    background: var(--overlay);
    color: var(--legend);
    border: 1px solid rgba(169, 164, 154, 0.6);
    border-radius: 8px;
    padding: 22px 36px;
    font-size: 22px;
    font-weight: 600;
    letter-spacing: 0.04em;
    box-shadow: 0 10px 40px rgba(0, 0, 0, 0.6);
  }
</style>
