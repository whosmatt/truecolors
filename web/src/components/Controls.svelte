<script lang="ts">
  import { store, patchScene } from '../lib/state.svelte';
  import Slider from './Slider.svelte';
</script>

<div class="card">
  <div class="head">
    <div class="card-title" style="margin:0">Controls</div>
    <button
      class="power"
      class:on={store.scene.power}
      onclick={() => patchScene({ power: !store.scene.power })}
      aria-pressed={store.scene.power}
    >
      <span class="dot"></span>
      <span class="pw-label">{store.scene.power ? 'On' : 'Off'}</span>
    </button>
  </div>

  <div class="sliders">
    <Slider
      label="Brightness"
      value={store.scene.brightness}
      accent="#ffd35c"
      oninput={(v) => patchScene({ brightness: v })}
    />
    <Slider
      label="Stretch"
      value={store.scene.stretch}
      accent="var(--accent-2)"
      oninput={(v) => patchScene({ stretch: v })}
    />
  </div>
</div>

<style>
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 16px;
  }
  .sliders {
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  /* power toggle (moved out of the header) */
  .power {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    padding: 9px 16px;
    border-radius: 999px;
    border: 1px solid var(--border-solid);
    background: var(--bg-elev-2);
    color: var(--text-dim);
    font-weight: 600;
    font-size: 0.88rem;
    transition: all 0.15s ease;
  }
  /* fixed-width label so toggling On/Off doesn't resize the button */
  .pw-label {
    min-width: 24px;
    text-align: left;
  }
  .power .dot {
    width: 9px;
    height: 9px;
    border-radius: 50%;
    background: var(--text-faint);
    transition: all 0.15s ease;
  }
  .power.on {
    background: rgba(46, 204, 113, 0.14);
    border-color: rgba(46, 204, 113, 0.5);
    color: var(--good);
  }
  .power.on .dot {
    background: var(--good);
    box-shadow: 0 0 10px var(--good);
  }
</style>
