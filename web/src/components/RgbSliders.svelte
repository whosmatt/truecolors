<script lang="ts">
  import { store, setRgb } from '../lib/state.svelte';
  import Slider from './Slider.svelte';

  const rgb = $derived(store.rgb);
</script>

<div class="card">
  <div class="head">
    <div class="card-title" style="margin:0">RGB</div>
  </div>
  <div class="sliders">
    <Slider
      label="Red"
      value={rgb.r}
      accent="#ff5161"
      trackFill="linear-gradient(90deg, var(--bg-elev-2), #ff5161)"
      oninput={(v) => setRgb({ ...rgb, r: v })}
    />
    <Slider
      label="Green"
      value={rgb.g}
      accent="#3ad37a"
      trackFill="linear-gradient(90deg, var(--bg-elev-2), #3ad37a)"
      oninput={(v) => setRgb({ ...rgb, g: v })}
    />
    <Slider
      label="Blue"
      value={rgb.b}
      accent="#5b8bff"
      trackFill="linear-gradient(90deg, var(--bg-elev-2), #5b8bff)"
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
  .sliders {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
</style>
