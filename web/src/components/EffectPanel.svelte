<script lang="ts">
  import {
    store,
    setEffect,
    setParam,
    patchScene,
    GLOBAL,
    type EffectParamDef,
  } from '../lib/state.svelte';
  import Slider from './Slider.svelte';

  const eff = $derived(store.currentEffect);

  // Which globals does the selected effect consume? (bitmask -> booleans)
  const showSpeed = $derived(!!eff && (eff.globals & GLOBAL.SPEED) !== 0);
  const showAudio = $derived(!!eff && (eff.globals & GLOBAL.AUDIO_SENS) !== 0);

  // Epilepsy-safe bounds and blocked effects come from the device's catalog;
  // the device also clamps at render, this only keeps the sliders honest.
  function paramMin(p: EffectParamDef): number {
    return store.epilepsySafe && p.safeMin != null
      ? Math.max(p.min, p.safeMin)
      : p.min;
  }

  function paramMax(p: EffectParamDef): number {
    return store.epilepsySafe && p.safeMax != null
      ? Math.min(p.max, p.safeMax)
      : p.max;
  }

  const effBlocked = $derived(store.epilepsySafe && !!eff?.epilepsyUnsafe);

  // Readout in the declared display unit. Decimals follow the scaled span;
  // symbol units attach directly ("120°"), letter units with a space.
  function fmtUnit(
    min: number,
    max: number,
    unit: string,
    scale: number,
    offset: number,
  ): (v: number) => string {
    const span = (max - min) * scale;
    const dec = span >= 100 ? 0 : span >= 10 ? 1 : 2;
    const suffix = unit ? (/^[a-z]/i.test(unit) ? ` ${unit}` : unit) : '';
    return (v) => `${(v * scale + offset).toFixed(dec)}${suffix}`;
  }

  // Labeled anchors at integer positions, blends in between ("bass→mid 30%").
  function fmtLabels(labels: string[]): (v: number) => string {
    return (v) => {
      const i = Math.max(0, Math.min(Math.floor(v), labels.length - 2));
      const f = v - i;
      if (f < 0.03) return labels[i];
      if (f > 0.97) return labels[i + 1];
      return `${labels[i]}→${labels[i + 1]} ${Math.round(f * 100)}%`;
    };
  }

  function fmtParam(p: EffectParamDef): (v: number) => string {
    if (p.labels && p.labels.length >= 2) return fmtLabels(p.labels);
    return fmtUnit(p.min, p.max, p.unit ?? '', p.scale ?? 1, p.offset ?? 0);
  }

  function onSelect(e: Event) {
    setEffect(parseInt((e.target as HTMLSelectElement).value, 10));
  }
</script>

<div class="card">
  <div class="card-title">Effect</div>

  {#if store.effects.length === 0}
    <p class="empty">Waiting for effect catalog…</p>
  {:else}
    <select value={store.scene.effectId} onchange={onSelect} aria-label="Effect">
      {#each store.effects as e, i (e.id)}
        <option value={i} disabled={store.epilepsySafe && e.epilepsyUnsafe}>
          {e.name}
        </option>
      {/each}
    </select>

    {#if effBlocked}
      <p class="blocked">Disabled by epilepsy-safe mode (see Settings).</p>
    {/if}

    <div class="controls">
      {#if showSpeed}
        <Slider
          label="Speed"
          value={store.scene.speed}
          accent="var(--accent)"
          format={eff?.speedUnit
            ? fmtUnit(0, 1, eff.speedUnit, eff.speedScale ?? 1, 0)
            : undefined}
          oninput={(v) => patchScene({ speed: v })}
        />
      {/if}
      {#if showAudio}
        <Slider
          label="RMS → brightness"
          value={store.scene.audioSens}
          oninput={(v) => patchScene({ audioSens: v })}
        />
      {/if}

      {#if eff}
        {#each eff.params as p, i (p.name)}
          {@const mn = paramMin(p)}
          {@const mx = paramMax(p)}
          <Slider
            label={p.name}
            value={Math.min(Math.max(store.scene.params[i] ?? p.def, mn), mx)}
            min={mn}
            max={mx}
            step={(mx - mn) / 1000}
            format={fmtParam(p)}
            oninput={(v) => setParam(i, v)}
          />
        {/each}
        {#if !showSpeed && !showAudio && eff.params.length === 0}
          <p class="empty">No adjustable parameters.</p>
        {/if}
      {/if}
    </div>
  {/if}
</div>

<style>
  .controls {
    display: flex;
    flex-direction: column;
    gap: 13px;
    margin-top: 16px;
  }
  .empty {
    color: var(--text-faint);
    font-size: 0.85rem;
    margin: 6px 0 0;
  }
  .blocked {
    color: var(--warn, #e0b352);
    font-size: 0.82rem;
    margin: 10px 0 0;
  }
</style>
