<script lang="ts">
  import { store, setRgb } from '../lib/state.svelte';
  import { rgbToCss } from '../lib/color';
  import Slider from './Slider.svelte';

  const rgb = $derived(store.rgb);
  const swatch = $derived(rgbToCss(rgb));
</script>

<div class="card">
  <div class="head">
    <div class="card-title" style="margin:0">RGB</div>
    <span class="swatch" style="background:{swatch}"></span>
  </div>
  <div class="sliders">
    <Slider
      label="Red"
      value={rgb.r}
      accent="#ff5161"
      trackFill="linear-gradient(90deg, #ff5161, var(--bg-elev-2))"
      oninput={(v) => setRgb({ ...rgb, r: v })}
    />
    <Slider
      label="Green"
      value={rgb.g}
      accent="#3ad37a"
      trackFill="linear-gradient(90deg, #3ad37a, var(--bg-elev-2))"
      oninput={(v) => setRgb({ ...rgb, g: v })}
    />
    <Slider
      label="Blue"
      value={rgb.b}
      accent="#5b8bff"
      trackFill="linear-gradient(90deg, #5b8bff, var(--bg-elev-2))"
      oninput={(v) => setRgb({ ...rgb, b: v })}
    />
  </div>
</div>

<style>
  .head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 14px;
  }
  .swatch {
    width: 26px;
    height: 26px;
    border-radius: 8px;
    border: 1px solid rgba(255, 255, 255, 0.2);
    box-shadow: 0 0 14px -3px currentColor;
  }
  .sliders {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
</style>
