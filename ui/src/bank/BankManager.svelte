<script lang="ts">
  // Bank manager overlay (§5.5, F-10).
  import { tick } from 'svelte';
  import { app } from '../model.svelte';
  import Overlay from '../overlay/Overlay.svelte';
  import { NUM_SLOTS, Perf, slotDisplay } from '../protocol';
  import { sendPatchBlockReason } from './sendPatch';

  const DRAG_TYPE = 'application/x-vjn6-slot';

  const core = $derived(app.core);
  const is106 = $derived(app.is106);
  const slots = Array.from({ length: NUM_SLOTS }, (_, i) => i + 1);
  const protect = $derived((core?.perf[Perf.PROTECT] ?? 1) !== 0);
  const sendReason = $derived(sendPatchBlockReason(app.core, app.env));

  let editing = $state<number | null>(null);
  let editText = $state('');
  let editInput: HTMLInputElement | undefined = $state();
  let focusRow = $state(1);
  let dropTarget = $state<number | 'edit' | null>(null);
  let listEl: HTMLDivElement | undefined = $state();
  let fileInput: HTMLInputElement | undefined = $state();

  $effect(() => {
    app.registerFileInput(fileInput ?? null);
    return () => app.registerFileInput(null);
  });

  // start at the current slot, scrolled into view (and follow it when another slot is selected)
  const currentSlot = $derived(app.core?.slot ?? 1);
  $effect(() => {
    const s = currentSlot;
    focusRow = s;
    tick().then(() => listEl?.querySelector(`[data-slot="${s}"]`)?.scrollIntoView({ block: 'nearest' }));
  });

  // closing the overlay commits a rename in progress (§5.5)
  $effect(() => () => commitRename());

  function nameOf(slot: number): string {
    return app.bank[slot - 1]?.n ?? '';
  }

  function startRename(slot: number) {
    if (editing !== null) commitRename();
    editing = slot;
    editText = nameOf(slot);
    tick().then(() => {
      editInput?.focus();
      editInput?.select();
    });
  }

  function commitRename() {
    if (editing === null) return;
    const slot = editing;
    const text = editText;
    editing = null;
    if (text !== nameOf(slot)) app.send({ t: 'rename', slot, name: text });
  }

  function cancelRename() {
    editing = null;
  }

  function onRenameKey(e: KeyboardEvent) {
    e.stopPropagation();
    if (e.key === 'Enter') {
      e.preventDefault();
      const slot = editing;
      commitRename();
      if (slot !== null) focusRowEl(slot);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      const slot = editing;
      cancelRename();
      if (slot !== null) focusRowEl(slot);
    }
  }

  function focusRowEl(slot: number) {
    focusRow = slot;
    tick().then(() => listEl?.querySelector<HTMLElement>(`[data-slot="${slot}"]`)?.focus());
  }

  function select(slot: number) {
    app.send({ t: 'selectSlot', slot });
  }

  function onRowKey(e: KeyboardEvent, slot: number) {
    if (editing !== null) return;
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      const next = Math.min(NUM_SLOTS, Math.max(1, slot + (e.key === 'ArrowDown' ? 1 : -1)));
      focusRowEl(next);
      tick().then(() => listEl?.querySelector(`[data-slot="${next}"]`)?.scrollIntoView({ block: 'nearest' }));
    } else if (e.key === 'Enter' && !e.altKey) {
      e.preventDefault();
      select(slot);
    } else if (e.key === 'F2') {
      e.preventDefault();
      startRename(slot);
    }
  }

  function onDragStart(e: DragEvent, slot: number) {
    if (!e.dataTransfer) return;
    e.dataTransfer.setData(DRAG_TYPE, String(slot));
    e.dataTransfer.setData('text/plain', `slot ${slot}`);
    e.dataTransfer.effectAllowed = 'copy';
  }

  function isSlotDrag(e: DragEvent): boolean {
    return !!e.dataTransfer && Array.from(e.dataTransfer.types).includes(DRAG_TYPE);
  }

  function onDragOver(e: DragEvent, target: number | 'edit') {
    if (!isSlotDrag(e)) return;
    e.preventDefault();
    e.stopPropagation();
    if (e.dataTransfer) e.dataTransfer.dropEffect = 'copy';
    dropTarget = target;
  }

  function onDrop(e: DragEvent, target: number | 'edit') {
    if (!isSlotDrag(e)) return;
    e.preventDefault();
    e.stopPropagation();
    dropTarget = null;
    const from = Number(e.dataTransfer?.getData(DRAG_TYPE));
    if (!Number.isInteger(from) || from < 1 || from > NUM_SLOTS) return;
    if (target === 'edit') app.send({ t: 'slotToEdit', slot: from });
    else if (target !== from) app.send({ t: 'copySlot', from, to: target });
  }

  function onFileChange() {
    const files = fileInput?.files;
    if (files && files.length) void app.loadFiles(Array.from(files));
    if (fileInput) fileInput.value = '';
  }

  const editLabel = $derived.by(() => {
    if (!core) return '';
    if (core.manual) return 'MANUAL';
    return slotDisplay(core.slot, is106) ?? `slot ${core.slot}`;
  });
</script>

<Overlay title="Bank manager" testid="bank-manager">
  <div class="bm">
    <header>
      <h2>BANK</h2>
      <span class="mode">{is106 ? '106' : '60'} mode</span>
    </header>

    <div class="list-head" aria-hidden="true">
      <span class="c-slot">SLOT</span><span class="c-disp">PATCH</span><span class="c-name">NAME</span>
    </div>
    <div class="list" bind:this={listEl} role="listbox" aria-label="Bank slots" data-testid="bank-list">
      {#each slots as slot (slot)}
        {@const disp = slotDisplay(slot, is106)}
        <div
          class="row"
          class:current={core?.slot === slot}
          class:grey={!is106 && slot > 56}
          class:drop={dropTarget === slot}
          role="option"
          tabindex={focusRow === slot ? 0 : -1}
          aria-label="Slot {slot} {disp ?? ''} {nameOf(slot)}"
          aria-selected={core?.slot === slot}
          data-testid="bank-row-{slot}"
          data-slot={slot}
          onkeydown={(e) => onRowKey(e, slot)}
          onfocus={() => (focusRow = slot)}
          ondragover={(e) => onDragOver(e, slot)}
          ondragleave={() => dropTarget === slot && (dropTarget = null)}
          ondrop={(e) => onDrop(e, slot)}
        >
          <span
            class="c-slot tabular"
            draggable="true"
            role="presentation"
            data-testid="bank-slot-{slot}"
            ondragstart={(e) => onDragStart(e, slot)}
            ondblclick={() => select(slot)}>{slot}</span
          >
          <span
            class="c-disp tabular"
            draggable="true"
            role="presentation"
            data-testid="bank-disp-{slot}"
            ondragstart={(e) => onDragStart(e, slot)}
            ondblclick={() => select(slot)}>{disp ?? '—'}</span
          >
          {#if editing === slot}
            <input
              bind:this={editInput}
              bind:value={editText}
              class="c-name rename"
              type="text"
              autocomplete="off"
              spellcheck="false"
              aria-label="Name of slot {slot}"
              data-testid="bank-rename"
              onkeydown={onRenameKey}
              onkeyup={(e) => e.stopPropagation()}
              onblur={commitRename}
            />
          {:else}
            <span
              class="c-name"
              role="presentation"
              data-testid="bank-name-{slot}"
              ondblclick={() => startRename(slot)}>{nameOf(slot)}</span
            >
          {/if}
        </div>
      {/each}
    </div>

    <aside>
      <div
        class="edit-target"
        class:drop={dropTarget === 'edit'}
        role="region"
        aria-label="Edit buffer"
        data-testid="bm-edit-target"
        ondragover={(e) => onDragOver(e, 'edit')}
        ondragleave={() => dropTarget === 'edit' && (dropTarget = null)}
        ondrop={(e) => onDrop(e, 'edit')}
      >
        <div class="et-title">EDIT BUFFER</div>
        <div class="et-name">
          <span class="tabular">{editLabel}</span>
          <span>{core?.name ?? ''}</span>{#if core?.edited}<span class="edited">EDITED</span>{/if}
        </div>
        <div class="et-hint">Drop a row here to load it</div>
      </div>

      <button type="button" data-testid="bm-load" onclick={() => app.openFilePicker()}>Load file…</button>
      <div class="filter">VJN6 patch (*.vjn6), VJN6 bank (*.vjn6bank), Juno-106 SysEx files (*.syx)</div>
      <input
        bind:this={fileInput}
        class="file"
        type="file"
        accept=".vjn6,.vjn6bank,.syx"
        tabindex="-1"
        aria-hidden="true"
        data-testid="bm-file-input"
        onchange={onFileChange}
      />
      <button type="button" data-testid="bm-export-bank-json" onclick={() => app.exportFile('bankJson')}
        >Export bank (.vjn6bank)</button
      >
      <button type="button" data-testid="bm-export-bank-syx" onclick={() => app.exportFile('bankSyx')}
        >Export bank (.syx)</button
      >
      <button type="button" data-testid="bm-export-patch-json" onclick={() => app.exportFile('patchJson')}
        >Export patch (.vjn6)</button
      >
      <button type="button" data-testid="bm-export-patch-syx" onclick={() => app.exportFile('patchSyx')}
        >Export patch (.syx)</button
      >
      <button
        type="button"
        data-testid="bm-send-patch"
        aria-disabled={sendReason ? 'true' : 'false'}
        title={sendReason || undefined}
        data-disabled-reason={sendReason}
        onclick={() => !sendReason && app.send({ t: 'sendPatch' })}>Send patch</button
      >
      <button
        type="button"
        class="toggle"
        aria-pressed={protect}
        data-testid="bm-protect"
        onclick={() => app.setPerf(Perf.PROTECT, protect ? 0 : 1, 'overlay')}
        >Memory protect: {protect ? 'ON' : 'OFF'}</button
      >
      <button type="button" class="close" data-testid="bm-close" onclick={() => app.closeOverlay()}>Close</button>
    </aside>

    <div class="notice" role="status" aria-live="polite" data-testid="bm-notice">
      {#if app.notice}{app.notice.text}{/if}
    </div>
  </div>
</Overlay>

<style>
  .bm {
    position: absolute;
    inset: 0;
    display: grid;
    grid-template-columns: 1fr 300px;
    grid-template-rows: 44px 24px 1fr 36px;
    column-gap: 20px;
    padding: 12px 20px 8px;
    box-sizing: border-box;
  }
  header {
    grid-column: 1 / 3;
    display: flex;
    align-items: baseline;
    gap: 14px;
  }
  h2 {
    margin: 0;
    font-size: 22px;
    font-weight: 600;
    letter-spacing: 0.12em;
  }
  .mode {
    color: var(--legend-dim);
    font-size: 14px;
  }
  .list-head,
  .row {
    display: grid;
    grid-template-columns: 56px 72px 1fr;
    align-items: center;
  }
  .list-head {
    grid-column: 1;
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.1em;
    color: var(--legend-dim);
    padding: 0 8px;
  }
  .list {
    grid-column: 1;
    grid-row: 3;
    overflow-y: auto;
    border: 1px solid rgba(169, 164, 154, 0.35);
    border-radius: 4px;
    background: rgba(0, 0, 0, 0.18);
  }
  .row {
    height: 26px;
    padding: 0 8px;
    font-size: 15px;
    border-bottom: 1px solid rgba(169, 164, 154, 0.08);
    color: var(--legend);
  }
  .row.grey {
    color: var(--legend-dim);
  }
  .row.current {
    background: rgba(255, 90, 72, 0.2);
    box-shadow: inset 3px 0 0 var(--lamp-on);
  }
  .row.drop {
    background: rgba(62, 123, 196, 0.35);
  }
  .row:focus-visible {
    outline-offset: -2px;
  }
  .c-slot,
  .c-disp {
    cursor: grab;
  }
  .c-name {
    white-space: pre;
    overflow: hidden;
    cursor: text;
  }
  .rename {
    height: 22px;
    box-sizing: border-box;
    font-size: 15px;
    color: var(--legend);
    background: var(--tooltip);
    border: 1px solid var(--legend);
    border-radius: 2px;
    padding: 0 4px;
    user-select: text;
    -webkit-user-select: text;
  }
  aside {
    grid-column: 2;
    grid-row: 2 / 4;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  .edit-target {
    border: 1px dashed var(--legend-dim);
    border-radius: 4px;
    padding: 6px 10px;
    margin-bottom: 4px;
  }
  .edit-target.drop {
    background: rgba(62, 123, 196, 0.35);
    border-style: solid;
  }
  .et-title {
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.1em;
    color: var(--legend-dim);
  }
  .et-name {
    display: flex;
    gap: 8px;
    font-size: 16px;
    white-space: pre;
  }
  .edited {
    color: var(--lamp-on);
    font-size: 12px;
    font-weight: 600;
    align-self: center;
  }
  .et-hint {
    font-size: 12px;
    color: var(--legend-dim);
  }
  aside button {
    height: 30px;
    font-size: 15px;
    font-weight: 600;
    color: var(--legend);
    background: rgba(236, 232, 220, 0.08);
    border: 1px solid rgba(169, 164, 154, 0.5);
    border-radius: 4px;
    text-align: left;
    padding: 0 12px;
    cursor: pointer;
  }
  aside button:hover {
    background: rgba(236, 232, 220, 0.14);
  }
  aside button[aria-disabled='true'] {
    color: var(--legend-dim);
    background: transparent;
    cursor: not-allowed;
  }
  .toggle[aria-pressed='true'] {
    border-color: var(--lamp-on);
  }
  .close {
    margin-top: auto;
  }
  .filter {
    font-size: 12px;
    line-height: 14px;
    color: var(--legend-dim);
    margin: -2px 2px 2px;
  }
  .file {
    display: none;
  }
  .notice {
    grid-column: 1 / 3;
    grid-row: 4;
    align-self: center;
    font-size: 15px;
    color: var(--legend);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
</style>
