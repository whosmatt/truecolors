<script lang="ts">
  import { store, setRgb } from '../lib/state.svelte';
  import { rgbToHsv, hsvToRgb } from '../lib/color';

  // Derive HSV from the shared RGB. Editing any HSV slider converts back to RGB
  // and writes it (which updates the chart + RGB sliders too).
  const hsv = $derived(rgbToHsv(store.rgb));

  function update(part: 'h' | 's' | 'v', value: number) {
    setRgb(hsvToRgb({ ...hsv, [part]: value }));
  }

  // Saturation/value track gradients reflect the current hue.
  const hueRgb = $derived(hsvToRgb({ h: hsv.h, s: 1, v: 1 }));
  const hueCss = $derived(
    `rgb(${Math.round(hueRgb.r * 255)},${Math.round(hueRgb.g * 255)},${Math.round(hueRgb.b * 255)})`,
  );
</script>

<div class="card">
  <div class="card-title">HSV</div>
  <div class="sliders">
    <div class="slider">
      <div class="row">
        <span class="label">Hue</span><span class="value"
          >{Math.round(hsv.h * 360)}°</span
        >
      </div>
      <input
        type="range"
        min="0"
        max="1"
        step="0.001"
        value={hsv.h}
        style="--track:linear-gradient(90deg,#ff0000,#ffff00,#00ff00,#00ffff,#0000ff,#ff00ff,#ff0000); --thumb:#fff"
        oninput={(e) => update('h', parseFloat(e.currentTarget.value))}
        aria-label="Hue"
      />
    </div>
    <div class="slider">
      <div class="row">
        <span class="label">Saturation</span><span class="value"
          >{Math.round(hsv.s * 100)}%</span
        >
      </div>
      <input
        type="range"
        min="0"
        max="1"
        step="0.001"
        value={hsv.s}
        style="--track:linear-gradient(90deg,#fff,{hueCss}); --thumb:{hueCss}"
        oninput={(e) => update('s', parseFloat(e.currentTarget.value))}
        aria-label="Saturation"
      />
    </div>
    <div class="slider">
      <div class="row">
        <span class="label">Value</span><span class="value"
          >{Math.round(hsv.v * 100)}%</span
        >
      </div>
      <input
        type="range"
        min="0"
        max="1"
        step="0.001"
        value={hsv.v}
        style="--track:linear-gradient(90deg,#000,{hueCss}); --thumb:{hueCss}"
        oninput={(e) => update('v', parseFloat(e.currentTarget.value))}
        aria-label="Value"
      />
    </div>
  </div>
</div>

<style>
  .sliders {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }
  .slider {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .row {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
  }
  .label {
    font-size: 0.82rem;
    color: var(--text-dim);
    font-weight: 500;
  }
  .value {
    font-size: 0.8rem;
    font-family: var(--mono);
    font-variant-numeric: tabular-nums;
  }
</style>
