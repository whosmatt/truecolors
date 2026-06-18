<script lang="ts">
  // Labelled range slider with a live value readout and an optional filled
  // track gradient (used for the color channel sliders).
  interface Props {
    label: string;
    value: number;
    min?: number;
    max?: number;
    step?: number;
    // Formatter for the readout (defaults to a 0..100% display when 0..1).
    format?: (v: number) => string;
    // CSS gradient for the track fill (e.g. R/G/B channels).
    trackFill?: string;
    accent?: string;
    disabled?: boolean;
    oninput: (v: number) => void;
  }

  let {
    label,
    value,
    min = 0,
    max = 1,
    step = 0.001,
    format,
    trackFill,
    accent,
    disabled = false,
    oninput,
  }: Props = $props();

  const pct = $derived(((value - min) / (max - min)) * 100);

  const readout = $derived.by(() => {
    if (format) return format(value);
    if (min === 0 && max === 1) return `${Math.round(value * 100)}%`;
    return value.toFixed(2);
  });

  // Track shows a filled portion up to the thumb. A custom trackFill overrides.
  const track = $derived(
    trackFill ??
      `linear-gradient(90deg, ${accent ?? 'var(--accent)'} 0 ${pct}%, var(--bg-elev-2) ${pct}% 100%)`,
  );

  function handle(e: Event) {
    oninput(parseFloat((e.target as HTMLInputElement).value));
  }
</script>

<div class="slider" class:disabled>
  <div class="row">
    <span class="label">{label}</span>
    <span class="value">{readout}</span>
  </div>
  <input
    type="range"
    {min}
    {max}
    {step}
    {value}
    {disabled}
    style="--track:{track}; --thumb:{accent ?? 'var(--accent)'}"
    oninput={handle}
    aria-label={label}
  />
</div>

<style>
  .slider {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .slider.disabled {
    opacity: 0.45;
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
    color: var(--text);
    font-variant-numeric: tabular-nums;
    font-family: var(--mono);
  }
</style>
