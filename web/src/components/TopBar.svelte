<script lang="ts">
  import { store, patchScene } from '../lib/state.svelte';
  import Slider from './Slider.svelte';
</script>

<header class="topbar card">
  <div class="brand">
    <div class="logo" aria-hidden="true"></div>
    <div class="title">
      <h1>TrueColors</h1>
      <span class="sub">RGB Laser Mood Light</span>
    </div>
  </div>

  <div class="globals">
    <button
      class="power"
      class:on={store.scene.power}
      onclick={() => patchScene({ power: !store.scene.power })}
      aria-pressed={store.scene.power}
    >
      <span class="dot"></span>
      {store.scene.power ? 'On' : 'Off'}
    </button>

    <div class="g-slider">
      <Slider
        label="Brightness"
        value={store.scene.brightness}
        accent="#ffd35c"
        oninput={(v) => patchScene({ brightness: v })}
      />
    </div>
    <div class="g-slider">
      <Slider
        label="Stretch"
        value={store.scene.stretch}
        accent="var(--accent-2)"
        oninput={(v) => patchScene({ stretch: v })}
      />
    </div>
  </div>
</header>

<style>
  .topbar {
    display: flex;
    align-items: center;
    gap: 22px;
    flex-wrap: wrap;
  }
  .brand {
    display: flex;
    align-items: center;
    gap: 13px;
    flex: 0 0 auto;
  }
  .logo {
    width: 38px;
    height: 38px;
    border-radius: 11px;
    background:
      conic-gradient(
        from 210deg,
        #ff4d5e,
        #ffd35c,
        #2ecc71,
        #21d4c4,
        #7c5cff,
        #ff4d5e
      );
    box-shadow: 0 0 22px -4px rgba(124, 92, 255, 0.6);
  }
  .title h1 {
    font-size: 1.15rem;
    line-height: 1.1;
  }
  .sub {
    font-size: 0.72rem;
    color: var(--text-faint);
    letter-spacing: 0.02em;
  }
  .globals {
    display: flex;
    align-items: center;
    gap: 20px;
    flex: 1 1 360px;
    min-width: 260px;
  }
  .g-slider {
    flex: 1 1 130px;
    min-width: 120px;
  }
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
    flex: 0 0 auto;
    transition: all 0.15s ease;
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
