<script lang="ts">
  import { store, setRgb } from '../lib/state.svelte';
  import {
    rgbToXy,
    xyToRgb,
    clampToGamut,
    PRIMARIES,
    rgbToCss,
  } from '../lib/color';
  import { locusPath, xyToSvg, svgToXy, type XY } from '../lib/cie';

  const SIZE = 320;

  // Static geometry computed once.
  const horseshoe = locusPath(SIZE);
  const triR = xyToSvg(PRIMARIES.r, SIZE);
  const triG = xyToSvg(PRIMARIES.g, SIZE);
  const triB = xyToSvg(PRIMARIES.b, SIZE);
  const triPath = `${triR.x.toFixed(1)},${triR.y.toFixed(1)} ${triG.x.toFixed(1)},${triG.y.toFixed(1)} ${triB.x.toFixed(1)},${triB.y.toFixed(1)}`;

  // The dot position is derived from the shared RGB via the forward map, so
  // external RGB edits (sliders) move it. Dragging writes RGB via the inverse.
  const dotXy = $derived(rgbToXy(store.rgb));
  const dot = $derived(xyToSvg(dotXy, SIZE));
  const dotColor = $derived(rgbToCss(store.rgb));

  let svgEl: SVGSVGElement;
  let dragging = $state(false);

  function pointerToXy(ev: PointerEvent): XY {
    const rect = svgEl.getBoundingClientRect();
    const sx = ((ev.clientX - rect.left) / rect.width) * SIZE;
    const sy = ((ev.clientY - rect.top) / rect.height) * SIZE;
    return clampToGamut(svgToXy({ x: sx, y: sy }, SIZE));
  }

  function applyFromPointer(ev: PointerEvent) {
    setRgb(xyToRgb(pointerToXy(ev)));
  }

  function onPointerDown(ev: PointerEvent) {
    dragging = true;
    svgEl.setPointerCapture(ev.pointerId);
    applyFromPointer(ev);
  }
  function onPointerMove(ev: PointerEvent) {
    if (!dragging) return;
    applyFromPointer(ev);
  }
  function onPointerUp(ev: PointerEvent) {
    dragging = false;
    try {
      svgEl.releasePointerCapture(ev.pointerId);
    } catch {
      /* capture may already be released */
    }
  }

  const vertexLabels = [
    { p: triR, t: 'R', dx: 8, dy: 4 },
    { p: triG, t: 'G', dx: -2, dy: -8 },
    { p: triB, t: 'B', dx: -16, dy: 4 },
  ];
</script>

<div class="card chart-card">
  <div class="card-title">Color · CIE 1931</div>
  <svg
    bind:this={svgEl}
    class="chart"
    viewBox="0 0 {SIZE} {SIZE}"
    role="application"
    aria-label="CIE chromaticity chart, drag to set color"
    onpointerdown={onPointerDown}
    onpointermove={onPointerMove}
    onpointerup={onPointerUp}
    onpointercancel={onPointerUp}
  >
    <defs>
      <radialGradient id="gamutGlow" cx="35%" cy="65%" r="75%">
        <stop offset="0%" stop-color="rgba(255,255,255,0.10)" />
        <stop offset="100%" stop-color="rgba(255,255,255,0)" />
      </radialGradient>
    </defs>

    <!-- Spectral-locus horseshoe outline -->
    <polygon
      points={horseshoe}
      fill="rgba(255,255,255,0.025)"
      stroke="rgba(255,255,255,0.18)"
      stroke-width="1"
      stroke-linejoin="round"
    />

    <!-- Device gamut triangle -->
    <polygon
      points={triPath}
      fill="url(#gamutGlow)"
      stroke="rgba(255,255,255,0.55)"
      stroke-width="1.4"
      stroke-linejoin="round"
    />

    <!-- Primary vertices -->
    {#each [triR, triG, triB] as v, i (i)}
      <circle
        cx={v.x}
        cy={v.y}
        r="3.2"
        fill={['#ff5161', '#3ad37a', '#5b8bff'][i]}
        stroke="#0a0a0f"
        stroke-width="1"
      />
    {/each}
    {#each vertexLabels as l (l.t)}
      <text
        x={l.p.x + l.dx}
        y={l.p.y + l.dy}
        class="vlabel"
        fill="var(--text-faint)">{l.t}</text
      >
    {/each}

    <!-- Draggable dot -->
    <circle
      cx={dot.x}
      cy={dot.y}
      r={dragging ? 11 : 9}
      fill={dotColor}
      stroke="#fff"
      stroke-width="2.5"
      class="dot"
      style="filter: drop-shadow(0 0 8px {dotColor})"
    />
  </svg>
  <div class="coords">
    x {dotXy.x.toFixed(3)} · y {dotXy.y.toFixed(3)}
  </div>
</div>

<style>
  .chart-card {
    display: flex;
    flex-direction: column;
    align-items: stretch;
  }
  .chart {
    width: 100%;
    height: auto;
    aspect-ratio: 1 / 1;
    touch-action: none;
    background:
      radial-gradient(
        110% 110% at 30% 70%,
        rgba(124, 92, 255, 0.06),
        transparent 70%
      );
    border-radius: var(--radius-sm);
    cursor: crosshair;
  }
  .dot {
    transition:
      r 0.08s ease,
      fill 0.05s linear;
  }
  .vlabel {
    font-size: 11px;
    font-weight: 700;
    font-family: var(--mono);
  }
  .coords {
    margin-top: 10px;
    text-align: center;
    font-family: var(--mono);
    font-size: 0.78rem;
    color: var(--text-faint);
    font-variant-numeric: tabular-nums;
  }
</style>
