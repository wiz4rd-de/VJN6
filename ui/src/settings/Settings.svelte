<script lang="ts">
  // Settings overlay (§5.6): mode at the top, the other §4.4 settings, MIDI receive channel, omni,
  // MIDI function (106) and memory protect, the device section and the status line. Changes apply
  // immediately.
  import { app } from '../model.svelte';
  import { shortenMiddle } from '../bank/notices';
  import Overlay from '../overlay/Overlay.svelte';
  import { Perf, Setting } from '../protocol';

  const core = $derived(app.core);
  const env = $derived(app.env);
  const s = (id: number) => core?.settings[id] ?? 0;
  const p = (id: number) => core?.perf[id] ?? 0;

  interface Choice {
    v: number;
    label: string;
  }

  const rows: { id: string; label: string; kind: 'setting' | 'perf'; key: number; choices: Choice[] }[] = [
    {
      id: 'chorus-noise',
      label: 'Chorus noise',
      kind: 'setting',
      key: Setting.CHORUS_NOISE,
      choices: [
        { v: 0, label: 'OFF' },
        { v: 1, label: 'HALF' },
        { v: 2, label: 'ORIGINAL' },
      ],
    },
    {
      id: 'velocity',
      label: 'Velocity → VCA',
      kind: 'setting',
      key: Setting.VELOCITY,
      choices: [
        { v: 0, label: 'Off' },
        { v: 1, label: 'On' },
      ],
    },
    {
      id: 'stereo',
      label: 'Stereo output',
      kind: 'setting',
      key: Setting.MONO,
      choices: [
        { v: 0, label: 'Stereo' },
        { v: 1, label: 'Mono' },
      ],
    },
    {
      id: 'oversampling',
      label: 'Oversampling',
      kind: 'setting',
      key: Setting.OVERSAMPLE,
      choices: [
        { v: 2, label: '2×' },
        { v: 4, label: '4×' },
      ],
    },
    {
      id: 'keyboard-play',
      label: 'Computer keyboard play',
      kind: 'setting',
      key: Setting.KBD_PLAY,
      choices: [
        { v: 0, label: 'Off' },
        { v: 1, label: 'On' },
      ],
    },
    {
      id: 'omni',
      label: 'Omni',
      kind: 'perf',
      key: Perf.OMNI,
      choices: [
        { v: 0, label: 'Off' },
        { v: 1, label: 'On' },
      ],
    },
    {
      id: 'midi-function',
      label: 'MIDI function (106)',
      kind: 'perf',
      key: Perf.MIDI_FUNCTION,
      choices: [
        { v: 1, label: 'I' },
        { v: 2, label: 'II' },
        { v: 3, label: 'III' },
      ],
    },
    {
      id: 'protect',
      label: 'Memory protect',
      kind: 'perf',
      key: Perf.PROTECT,
      choices: [
        { v: 0, label: 'Off' },
        { v: 1, label: 'On' },
      ],
    },
  ];

  const variations = Array.from({ length: 11 }, (_, i) => i * 10);
  const zooms = [0, ...Array.from({ length: 16 }, (_, i) => 50 + i * 10)];
  const channels = Array.from({ length: 16 }, (_, i) => i + 1);

  function current(kind: 'setting' | 'perf', key: number): number {
    return kind === 'setting' ? s(key) : p(key);
  }

  function choose(kind: 'setting' | 'perf', key: number, v: number) {
    if (kind === 'setting') app.setSetting(key, v);
    else app.setPerf(key, v, 'overlay');
  }

  const isWeb = $derived(env.build === 'web');
  const midiOk = $derived(env.midiAvailable);
  const midiPending = $derived(!!app.env.midiPending);
</script>

<Overlay title="Settings" testid="settings">
  <div class="st">
    <header>
      <h2>SETTINGS</h2>
      <div class="mode" role="group" aria-label="Mode">
        <span class="mode-label">Mode</span>
        {#each [0, 1] as m (m)}
          <button
            type="button"
            class="mode-btn"
            aria-pressed={s(Setting.MODE) === m}
            data-testid="set-mode-{m ? '106' : '60'}"
            onclick={() => app.setSetting(Setting.MODE, m)}>{m ? '106' : '60'}</button
          >
        {/each}
      </div>
    </header>

    <div class="cols">
      <div class="col">
        {#each rows.slice(0, 5) as r (r.id)}
          <div class="row">
            <span class="lbl" id="lbl-{r.id}">{r.label}</span>
            <div class="seg" role="group" aria-labelledby="lbl-{r.id}" data-testid="set-{r.id}">
              {#each r.choices as c (c.v)}
                <button
                  type="button"
                  aria-pressed={current(r.kind, r.key) === c.v}
                  data-testid="set-{r.id}-{c.v}"
                  onclick={() => choose(r.kind, r.key, c.v)}>{c.label}</button
                >
              {/each}
            </div>
          </div>
        {/each}
        <div class="row">
          <label class="lbl" for="set-variation">Voice variation</label>
          <select
            id="set-variation"
            data-testid="set-variation"
            value={s(Setting.VARIATION)}
            onchange={(e) => app.setSetting(Setting.VARIATION, Number((e.currentTarget as HTMLSelectElement).value))}
          >
            {#each variations as v (v)}<option value={v}>{v} %</option>{/each}
          </select>
        </div>
        <div class="row">
          <label class="lbl" for="set-zoom">Zoom</label>
          <select
            id="set-zoom"
            data-testid="set-zoom"
            value={s(Setting.ZOOM)}
            onchange={(e) => app.setSetting(Setting.ZOOM, Number((e.currentTarget as HTMLSelectElement).value))}
          >
            {#each zooms as z (z)}<option value={z}>{z === 0 ? 'Fit' : `${z} %`}</option>{/each}
          </select>
        </div>
      </div>

      <div class="col">
        <div class="row">
          <label class="lbl" for="set-channel">MIDI receive channel</label>
          <select
            id="set-channel"
            data-testid="set-channel"
            value={p(Perf.MIDI_CHANNEL)}
            onchange={(e) =>
              app.setPerf(Perf.MIDI_CHANNEL, Number((e.currentTarget as HTMLSelectElement).value), 'overlay')}
          >
            {#each channels as ch (ch)}<option value={ch}>{ch}</option>{/each}
          </select>
        </div>
        {#each rows.slice(5) as r (r.id)}
          <div class="row">
            <span class="lbl" id="lbl-{r.id}">{r.label}</span>
            <div class="seg" role="group" aria-labelledby="lbl-{r.id}" data-testid="set-{r.id}">
              {#each r.choices as c (c.v)}
                <button
                  type="button"
                  aria-pressed={current(r.kind, r.key) === c.v}
                  data-testid="set-{r.id}-{c.v}"
                  onclick={() => choose(r.kind, r.key, c.v)}>{c.label}</button
                >
              {/each}
            </div>
          </div>
        {/each}

        {#if env.build !== 'plugin'}
          <div class="devices" data-testid="set-devices">
            <div class="dev-title">Devices</div>
            {#if env.build === 'standalone'}
              <button type="button" class="wide" data-testid="set-open-devices" onclick={() => app.send({ t: 'openDevices' })}
                >Audio/MIDI devices…</button
              >
            {/if}
            {#if isWeb && !midiOk}
              {#if !midiPending}
                <div class="state" data-testid="set-midi-state">MIDI not available — use the computer keyboard.</div>
              {/if}
            {:else}
              {#if isWeb}
                <div class="inputs" role="group" aria-label="MIDI inputs" data-testid="set-midi-inputs">
                  {#each env.inputs as inp, i (inp.id)}
                    <label class="check" title={inp.name}>
                      <input
                        type="checkbox"
                        checked={inp.enabled !== false}
                        data-testid="set-midi-input-{i}"
                        onchange={(e) =>
                          app.web?.midi?.setInputEnabled(inp.id, (e.currentTarget as HTMLInputElement).checked)}
                      />
                      <span>{shortenMiddle(inp.name, 40)}</span>
                    </label>
                  {/each}
                  {#if env.inputs.length === 0}<div class="none">No MIDI inputs</div>{/if}
                </div>
              {/if}
              <div class="row">
                <label class="lbl" for="set-midi-output">MIDI output</label>
                <select
                  id="set-midi-output"
                  data-testid="set-midi-output"
                  value={env.output}
                  title={env.outputs.find((o) => o.id === env.output)?.name ?? 'None'}
                  onchange={(e) => {
                    const id = (e.currentTarget as HTMLSelectElement).value;
                    if (isWeb) app.web?.midi?.setOutput(id);
                    else app.send({ t: 'midiOutput', name: id });
                  }}
                >
                  <option value="">None</option>
                  {#each env.outputs as o (o.id)}<option value={o.id} title={o.name}>{shortenMiddle(o.name, 40)}</option>{/each}
                </select>
              </div>
              {#if isWeb && !env.sysexPermitted}
                <div class="state" data-testid="set-sysex-state">SysEx not permitted — SysEx disabled.</div>
              {/if}
            {/if}
          </div>
        {/if}
      </div>
    </div>

    <footer>
      <div class="status" role="status" aria-live="polite" data-testid="set-status">{app.status}</div>
      <button type="button" class="close" data-testid="set-close" onclick={() => app.closeOverlay()}>Close</button>
    </footer>
  </div>
</Overlay>

<style>
  .st {
    position: absolute;
    inset: 0;
    display: flex;
    flex-direction: column;
    padding: 12px 24px 12px;
    box-sizing: border-box;
  }
  header {
    display: flex;
    align-items: center;
    gap: 32px;
    height: 48px;
  }
  h2 {
    margin: 0;
    font-size: 22px;
    font-weight: 600;
    letter-spacing: 0.12em;
  }
  .mode {
    display: flex;
    align-items: center;
    gap: 8px;
  }
  .mode-label {
    color: var(--legend-dim);
    margin-right: 6px;
  }
  .mode-btn {
    width: 72px;
    height: 34px;
    font-family: 'Oswald', sans-serif;
    font-size: 18px;
  }
  .cols {
    flex: 1;
    display: grid;
    grid-template-columns: 1fr 1fr;
    column-gap: 40px;
    margin-top: 10px;
    min-height: 0;
  }
  .col {
    display: flex;
    flex-direction: column;
    gap: 7px;
    min-height: 0;
  }
  .row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    min-height: 30px;
  }
  .lbl {
    color: var(--legend);
  }
  .seg {
    display: flex;
    gap: 0;
  }
  button {
    height: 28px;
    font-size: 14px;
    font-weight: 600;
    color: var(--legend);
    background: rgba(236, 232, 220, 0.06);
    border: 1px solid rgba(169, 164, 154, 0.5);
    padding: 0 12px;
    cursor: pointer;
  }
  .seg button {
    border-radius: 0;
    margin-left: -1px;
    min-width: 46px;
  }
  .seg button:first-child {
    border-radius: 4px 0 0 4px;
    margin-left: 0;
  }
  .seg button:last-child {
    border-radius: 0 4px 4px 0;
  }
  button[aria-pressed='true'] {
    background: rgba(255, 90, 72, 0.28);
    border-color: var(--lamp-on);
    position: relative;
  }
  .mode-btn,
  .wide,
  .close {
    border-radius: 4px;
  }
  select {
    height: 28px;
    min-width: 120px;
    max-width: 260px;
    font-size: 14px;
    color: var(--legend);
    background: var(--tooltip);
    border: 1px solid rgba(169, 164, 154, 0.5);
    border-radius: 4px;
    padding: 0 6px;
  }
  .devices {
    margin-top: 6px;
    padding-top: 6px;
    border-top: 1px solid rgba(169, 164, 154, 0.3);
    display: flex;
    flex-direction: column;
    gap: 6px;
    min-height: 0;
  }
  .dev-title {
    font-size: 12px;
    font-weight: 600;
    letter-spacing: 0.1em;
    color: var(--legend-dim);
    text-transform: uppercase;
  }
  .inputs {
    max-height: 70px;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .check {
    display: flex;
    align-items: center;
    gap: 6px;
    white-space: nowrap;
  }
  .none {
    color: var(--legend-dim);
    font-size: 14px;
  }
  .state {
    color: var(--legend);
    font-size: 14px;
  }
  .wide {
    align-self: flex-start;
  }
  footer {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    height: 36px;
  }
  .status {
    font-size: 14px;
    color: var(--legend);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
</style>
