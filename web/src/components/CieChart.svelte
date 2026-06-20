<script lang="ts">
  import { store, setRgb } from '../lib/state.svelte';
  import {
    rgbToXy,
    xyToRgb,
    clampToGamut,
    PRIMARIES,
    rgbToCss,
  } from '../lib/color';
  import {
    locusPath,
    xyToSvg,
    svgToXy,
    locusLabels,
    XY_DOMAIN,
    type XY,
  } from '../lib/cie';

  const SIZE = 320;

  // Static geometry computed once.
  const horseshoe = locusPath(SIZE);
  const triR = xyToSvg(PRIMARIES.r, SIZE);
  const triG = xyToSvg(PRIMARIES.g, SIZE);
  const triB = xyToSvg(PRIMARIES.b, SIZE);
  const triPath = `${triR.x.toFixed(1)},${triR.y.toFixed(1)} ${triG.x.toFixed(1)},${triG.y.toFixed(1)} ${triB.x.toFixed(1)},${triB.y.toFixed(1)}`;

  // Reference gamuts for comparison, drawn as thin gray outlines.
  const refGamuts = [
    { name: 'sRGB', r: { x: 0.64, y: 0.33 }, g: { x: 0.3, y: 0.6 }, b: { x: 0.15, y: 0.06 } },
    // { name: 'P3', r: { x: 0.68, y: 0.32 }, g: { x: 0.265, y: 0.69 }, b: { x: 0.15, y: 0.06 } },
  ].map((gm) => {
    const R = xyToSvg(gm.r, SIZE);
    const G = xyToSvg(gm.g, SIZE);
    const B = xyToSvg(gm.b, SIZE);
    return {
      name: gm.name,
      points: `${R.x.toFixed(1)},${R.y.toFixed(1)} ${G.x.toFixed(1)},${G.y.toFixed(1)} ${B.x.toFixed(1)},${B.y.toFixed(1)}`,
      lx: G.x - 4,
      ly: G.y - 5,
    };
  });

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
    if (dragging) applyFromPointer(ev);
  }
  function onPointerUp(ev: PointerEvent) {
    dragging = false;
    try {
      svgEl.releasePointerCapture(ev.pointerId);
    } catch {
      /* capture may already be released */
    }
  }

  // Accurate chromaticity fill: for each pixel, map to xy, convert to sRGB at
  // full chroma (Y=1, normalize peak), gamma encode. Rendered to a canvas once
  // and clipped to the spectral locus.
  let gradientUrl = $state('');
  function chromaToRgb(x: number, y: number): [number, number, number] | null {
    if (y <= 1e-4) return null;
    const X = x / y;
    const Z = (1 - x - y) / y;
    let r = 3.2406 * X - 1.5372 - 0.4986 * Z;
    let g = -0.9689 * X + 1.8758 + 0.0415 * Z;
    let b = 0.0557 * X - 0.204 + 1.057 * Z;
    r = Math.max(0, r);
    g = Math.max(0, g);
    b = Math.max(0, b);
    const m = Math.max(r, g, b);
    if (m <= 0) return null;
    const enc = (v: number) => Math.round(Math.pow(v / m, 1 / 2.2) * 255);
    return [enc(r), enc(g), enc(b)];
  }
  $effect(() => {
    const canvas = document.createElement('canvas');
    canvas.width = SIZE;
    canvas.height = SIZE;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const img = ctx.createImageData(SIZE, SIZE);
    for (let py = 0; py < SIZE; py++) {
      for (let px = 0; px < SIZE; px++) {
        const xy = svgToXy({ x: px + 0.5, y: py + 0.5 }, SIZE);
        const c = chromaToRgb(xy.x, xy.y);
        const i = (py * SIZE + px) * 4;
        if (c) {
          img.data[i] = c[0];
          img.data[i + 1] = c[1];
          img.data[i + 2] = c[2];
          img.data[i + 3] = 255;
        }
      }
    }
    ctx.putImageData(img, 0, 0);
    gradientUrl = canvas.toDataURL();
  });

  // Axis ticks (chromaticity values mapped to svg).
  const xTicks = [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7];
  const yTicks = [0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8];
  const axisX0 = xyToSvg({ x: XY_DOMAIN.xMin, y: XY_DOMAIN.yMin }, SIZE);
  const axisTop = xyToSvg({ x: XY_DOMAIN.xMin, y: XY_DOMAIN.yMax }, SIZE);
  const axisRight = xyToSvg({ x: XY_DOMAIN.xMax, y: XY_DOMAIN.yMin }, SIZE);

  // Wavelength labels offset outward from the white point.
  const white = xyToSvg({ x: 0.31, y: 0.33 }, SIZE);
  const labels = locusLabels().map((l) => {
    const p = xyToSvg(l.p, SIZE);
    const dx = p.x - white.x;
    const dy = p.y - white.y;
    const len = Math.hypot(dx, dy) || 1;
    const ux = dx / len;
    const uy = dy / len;
    return {
      nm: l.nm,
      x1: p.x,
      y1: p.y,
      x2: p.x + ux * 6,
      y2: p.y + uy * 6,
      tx: p.x + ux * 15,
      ty: p.y + uy * 15,
    };
  });

  const vertexLabels = [
    { p: triR, t: 'R', dx: 9, dy: 4 },
    { p: triG, t: 'G', dx: 0, dy: -8 },
    { p: triB, t: 'B', dx: -14, dy: 12 },
  ];
</script>

<div class="card chart-card">
  <div class="card-title">CIE 1931</div>
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
      <clipPath id="locusClip">
        <polygon points={horseshoe} />
      </clipPath>
    </defs>

    <!-- Accurate chromaticity gradient, clipped to the spectral locus -->
    {#if gradientUrl}
      <image
        href={gradientUrl}
        x="0"
        y="0"
        width={SIZE}
        height={SIZE}
        clip-path="url(#locusClip)"
        opacity="0.92"
      />
    {/if}

    <!-- Axes -->
    <g class="axes">
      <line x1={axisX0.x} y1={axisX0.y} x2={axisRight.x} y2={axisRight.y} />
      <line x1={axisX0.x} y1={axisX0.y} x2={axisTop.x} y2={axisTop.y} />
      {#each xTicks as t (t)}
        {@const s = xyToSvg({ x: t, y: XY_DOMAIN.yMin }, SIZE)}
        <line x1={s.x} y1={s.y} x2={s.x} y2={s.y + 4} />
        {#if Math.round(t * 10) % 2 === 0}
          <text x={s.x} y={s.y + 15} class="tick" text-anchor="middle"
            >{t.toFixed(1)}</text
          >
        {/if}
      {/each}
      {#each yTicks as t (t)}
        {@const s = xyToSvg({ x: XY_DOMAIN.xMin, y: t }, SIZE)}
        <line x1={s.x} y1={s.y} x2={s.x - 4} y2={s.y} />
        {#if Math.round(t * 10) % 2 === 0}
          <text x={s.x - 7} y={s.y + 3} class="tick" text-anchor="end"
            >{t.toFixed(1)}</text
          >
        {/if}
      {/each}
      <text x={axisRight.x} y={axisRight.y + 24} class="axis-title" text-anchor="end">x</text>
      <text x={axisTop.x - 18} y={axisTop.y + 2} class="axis-title">y</text>
    </g>

    <!-- Spectral-locus outline -->
    <polygon
      points={horseshoe}
      fill="none"
      stroke="rgba(255,255,255,0.45)"
      stroke-width="1"
      stroke-linejoin="round"
    />

    <!-- Wavelength labels -->
    <g class="waves">
      {#each labels as l (l.nm)}
        <line x1={l.x1} y1={l.y1} x2={l.x2} y2={l.y2} />
        <text x={l.tx} y={l.ty} text-anchor="middle" dominant-baseline="middle"
          >{l.nm}</text
        >
      {/each}
    </g>

    <!-- Reference gamuts (sRGB, P3) -->
    {#each refGamuts as g (g.name)}
      <polygon class="ref" points={g.points} />
      <text class="ref-label" x={g.lx} y={g.ly} text-anchor="end">{g.name}</text>
    {/each}

    <!-- Device gamut triangle -->
    <polygon
      points={triPath}
      fill="none"
      stroke="rgb(0, 0, 0, 0.6)"
      stroke-width="1.2"
      stroke-linejoin="round"
    />
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
      <text x={l.p.x + l.dx} y={l.p.y + l.dy} class="vlabel">{l.t}</text>
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
    user-select: none;
    -webkit-user-select: none;
    background: var(--bg-elev-1, #14141b);
    border-radius: var(--radius-sm);
    cursor: crosshair;
  }
  .axes line {
    stroke: rgba(255, 255, 255, 0.22);
    stroke-width: 1;
  }
  .tick {
    font-size: 9px;
    font-family: var(--mono);
    fill: var(--text-faint);
  }
  .axis-title {
    font-size: 11px;
    font-style: italic;
    fill: var(--text-faint);
  }
  .waves line {
    stroke: rgba(255, 255, 255, 0.3);
    stroke-width: 1;
  }
  .waves text {
    font-size: 8.5px;
    font-family: var(--mono);
    fill: rgba(255, 255, 255, 0.6);
  }
  .ref {
    fill: none;
    stroke: rgba(0, 0, 0, 0.5);
    stroke-width: 0.8;
    stroke-linejoin: round;
  }
  .ref-label {
    font-size: 8.5px;
    font-family: var(--mono);
    fill: rgba(0, 0, 0, 0.5);
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
    fill: var(--text-faint);
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
