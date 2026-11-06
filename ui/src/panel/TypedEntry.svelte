<script lang="ts">
  // Typed value entry on a double-clicked slider or knob (R-U1): Enter commits, Esc reverts,
  // leaving the field commits; non-numbers revert (handled by the request's commit).
  import { app } from '../model.svelte';

  const req = $derived(app.typed);
  let input: HTMLInputElement | undefined = $state();
  let done = false;

  $effect(() => {
    if (req && input) {
      done = false;
      input.value = req.initial;
      input.focus();
      input.select();
    }
  });

  function finish(commit: boolean) {
    if (done || !req) return;
    done = true;
    const r = req;
    const text = input?.value ?? '';
    app.closeTyped();
    if (commit) r.commit(text);
  }

  function onKey(e: KeyboardEvent) {
    e.stopPropagation();
    if (e.key === 'Enter') {
      e.preventDefault();
      finish(true);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      finish(false);
    }
  }
</script>

{#if req}
  <input
    bind:this={input}
    class="typed tabular"
    type="text"
    inputmode="decimal"
    autocomplete="off"
    spellcheck="false"
    data-testid="typed-entry"
    aria-label="Value"
    style="left:{req.x}px; top:{req.y}px; width:{req.w}px"
    onkeydown={onKey}
    onkeyup={(e) => e.stopPropagation()}
    onblur={() => finish(true)}
  />
{/if}

<style>
  .typed {
    position: absolute;
    z-index: 70;
    height: 20px;
    box-sizing: border-box;
    padding: 0 4px;
    font-size: 13px;
    font-weight: 600;
    text-align: center;
    color: var(--legend);
    background: var(--tooltip);
    border: 1px solid var(--legend);
    border-radius: 2px;
    outline: none;
    user-select: text;
    -webkit-user-select: text;
  }
</style>
