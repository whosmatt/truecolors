<script lang="ts">
  // Live grid debug view. Marks sit at their phase within one beat (locked)
  // or in a fixed window (unlocked) and fade; only the cursor moves. The fold
  // is a beat, not a bar. Below, the phase error of each estimate.
  import { onMount } from 'svelte';
  import { store } from '../lib/state.svelte';

  const UNLOCKED_WIN_S = 4; // fallback window while unlocked
  const FADE_LOOPS = 4; // locked marks live this many beats
  const FADE_S = 8; // unlocked marks live this many seconds
  const JITTER_N = 64; // PLL errors shown in the strip

  let canvas: HTMLCanvasElement;
  let wrap: HTMLDivElement;

  // Device block-time now, extrapolated from the last message.
  function deviceNow(): number | null {
    const l = store.bgLast;
    if (!l) return null;
    return l.t + ((performance.now() - l.rx) / 1000) * l.blockHz;
  }

  onMount(() => {
    const ctx = canvas.getContext('2d')!;
    const css = getComputedStyle(document.documentElement);
    const col = {
      kick: css.getPropertyValue('--accent').trim() || '#7c5cff',
      snare: css.getPropertyValue('--accent-2').trim() || '#4cc2ff',
      hihat: css.getPropertyValue('--warn').trim() || '#e0a33c',
      act: css.getPropertyValue('--text-dim').trim() || '#8899aa',
      met: css.getPropertyValue('--good').trim() || '#2ecc71',
      dim: css.getPropertyValue('--text-faint').trim() || '#666',
      text: css.getPropertyValue('--text-dim').trim() || '#aaa',
      line: 'rgba(255,255,255,0.08)',
      cursor: 'rgba(255,255,255,0.75)',
    };
    const lanes: Array<{ key: 'kick' | 'snare' | 'hihat' | 'act' | 'met'; label: string }> = [
      { key: 'kick', label: 'kick' },
      { key: 'snare', label: 'snare' },
      { key: 'hihat', label: 'hihat' },
      { key: 'act', label: 'beat act' },
      { key: 'met', label: 'metro' },
    ];

    let W = 0;
    let H = 0;
    const ro = new ResizeObserver(() => {
      const dpr = window.devicePixelRatio || 1;
      W = wrap.clientWidth;
      H = 220;
      canvas.width = Math.round(W * dpr);
      canvas.height = Math.round(H * dpr);
      canvas.style.width = `${W}px`;
      canvas.style.height = `${H}px`;
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    });
    ro.observe(wrap);

    const wrap01 = (v: number) => ((v % 1) + 1) % 1;

    let raf = 0;
    const draw = () => {
      raf = requestAnimationFrame(draw);
      if (W === 0) return;
      ctx.clearRect(0, 0, W, H);

      const rollH = H - 46;
      const laneH = rollH / lanes.length;
      const stripY = rollH + 10;
      const stripH = H - stripY;
      const l = store.bgLast;
      const evs = store.bgEvents;
      const dnow = deviceNow();
      const isLocked = !!l && l.period > 0;
      const winBlocks = l ? UNLOCKED_WIN_S * l.blockHz : 1;

      // lane separators + labels
      ctx.font = '10px sans-serif';
      ctx.textBaseline = 'top';
      for (let i = 0; i < lanes.length; i++) {
        const y = i * laneH;
        if (i > 0) {
          ctx.fillStyle = col.line;
          ctx.fillRect(0, y, W, 1);
        }
        ctx.fillStyle = col.dim;
        ctx.fillText(lanes[i].label, 4, y + 3);
      }
      ctx.fillStyle = col.line;
      ctx.fillRect(0, rollH, W, 1);

      if (l && dnow !== null) {
        // event marks; kick/snare carry a sub-block time offset (ev.off)
        for (const ev of evs) {
          let alpha: number;
          let posHit: number;
          let posMet: number;
          if (isLocked) {
            if (ev.period <= 0) continue;
            posHit = wrap01((ev.phase + ev.off) / ev.period);
            posMet = wrap01(ev.phase / ev.period);
            alpha = 1 - (dnow - ev.t) / l.period / FADE_LOOPS;
          } else {
            posHit = wrap01((ev.t + ev.off) / winBlocks);
            posMet = wrap01(ev.t / winBlocks);
            alpha = 1 - (dnow - ev.t) / l.blockHz / FADE_S;
          }
          if (alpha <= 0) continue;
          for (let i = 0; i < lanes.length; i++) {
            const key = lanes[i].key;
            const top = i * laneH + 14;
            const full = laneH - 17;
            if (key === 'act') {
              // Height is the activation stage 2 consumes, never thresholded.
              const a = ev.act ?? 0;
              if (a <= 0.01) continue;
              ctx.globalAlpha = alpha * 0.85;
              ctx.fillStyle = col.act;
              const h = Math.max(1, a * full);
              ctx.fillRect(posHit * W - 1, top + (full - h), 2, h);
              continue;
            }
            if (!ev[key]) continue;
            const x = (key === 'met' ? posMet : posHit) * W;
            ctx.globalAlpha = alpha;
            ctx.fillStyle = col[key];
            ctx.fillRect(x - 1, top, 2, full);
          }
        }
        ctx.globalAlpha = 1;

        // cursor
        const cx = isLocked
          ? wrap01((dnow - l.t + l.phase) / l.period)
          : wrap01(dnow / winBlocks);
        ctx.fillStyle = col.cursor;
        ctx.fillRect(cx * W - 0.5, 0, 1, rollH);
      }

      // PLL jitter strip: last N phase errors (ms), newest right.
      // err only changes when an observation lands.
      const errs: typeof evs = [];
      let lastErr: number | null = null;
      for (const e of evs) {
        if (e.err !== lastErr) {
          errs.push(e);
          lastErr = e.err;
        }
      }
      errs.splice(0, Math.max(0, errs.length - JITTER_N));
      const mid = stripY + stripH / 2;
      ctx.fillStyle = col.line;
      ctx.fillRect(0, mid, W, 1);
      if (errs.length > 0) {
        const ms = errs.map((e) => (e.err / e.blockHz) * 1000);
        const scale = Math.max(10, ...ms.map(Math.abs)); // >= ±10 ms range
        const rms = Math.sqrt(ms.reduce((a, v) => a + v * v, 0) / ms.length);
        for (let i = 0; i < ms.length; i++) {
          const x = W - (ms.length - i) * 5 - 4;
          if (x < 40) continue;
          const y = mid - (ms[i] / scale) * (stripH / 2 - 2);
          ctx.fillStyle = col.kick;
          ctx.fillRect(x - 1, Math.min(mid, y), 2, Math.max(1, Math.abs(y - mid)));
        }
        ctx.fillStyle = col.text;
        ctx.textBaseline = 'middle';
        ctx.fillText(`grid err ±${rms.toFixed(1)} ms rms`, 4, mid);
      } else {
        ctx.fillStyle = col.dim;
        ctx.textBaseline = 'middle';
        ctx.fillText('grid err —', 4, mid);
      }
    };
    raf = requestAnimationFrame(draw);

    return () => {
      cancelAnimationFrame(raf);
      ro.disconnect();
    };
  });
</script>

<div class="wrap" bind:this={wrap}>
  <canvas bind:this={canvas}></canvas>
</div>

<style>
  .wrap {
    margin: 8px 0 4px;
  }
  canvas {
    display: block;
    width: 100%;
    border-radius: var(--radius-sm);
    background: var(--bg-elev-2);
  }
</style>
