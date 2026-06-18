<script lang="ts">
  import {
    store,
    setEffect,
    setParam,
    patchScene,
    GLOBAL,
  } from '../lib/state.svelte';
  import Slider from './Slider.svelte';

  const eff = $derived(store.currentEffect);

  // Which globals does the selected effect consume? (bitmask -> booleans)
  const showSpeed = $derived(!!eff && (eff.globals & GLOBAL.SPEED) !== 0);
  const showAudio = $derived(!!eff && (eff.globals & GLOBAL.AUDIO_SENS) !== 0);

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
        <option value={i}>{e.name}</option>
      {/each}
    </select>

    <div class="controls">
      {#if showSpeed}
        <Slider
          label="Speed"
          value={store.scene.speed}
          accent="var(--accent)"
          oninput={(v) => patchScene({ speed: v })}
        />
      {/if}
      {#if showAudio}
        <Slider
          label="Audio Sensitivity"
          value={store.scene.audioSens}
          accent="var(--accent-2)"
          oninput={(v) => patchScene({ audioSens: v })}
        />
      {/if}

      {#if eff}
        {#each eff.params as p, i (p.name)}
          <Slider
            label={p.name}
            value={store.scene.params[i] ?? p.def}
            min={p.min}
            max={p.max}
            step={(p.max - p.min) / 1000}
            format={(v) => v.toFixed(2)}
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
</style>
