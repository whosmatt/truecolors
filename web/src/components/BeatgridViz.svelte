<script lang="ts">
  // Live beatgrid debug view (embedded in the Telemetry card). Piano-roll of
  // kick / snare / metronome events: marks sit at their loop phase (locked)
  // or in a fixed window (unlocked) and fade out; only the cursor moves.
  // Below, a strip chart of the PLL phase error at each matched hit shows
  // how jittery the lock is.
  import { onMount } from 'svelte';
  import { store } from '../lib/state.svelte';

  const UNLOCKED_WIN_S = 4; // fallback window while unlocked
  const FADE_LOOPS = 4; // locked marks live this many loops
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
      met: css.getPropertyValue('--good').trim() || '#2ecc71',
      dim: css.getPropertyValue('--text-faint').trim() || '#666',
      text: css.getPropertyValue('--text-dim').trim() || '#aaa',
      line: 'rgba(255,255,255,0.08)',
      cursor: 'rgba(255,255,255,0.75)',
    };
    const lanes: Array<{ key: 'kick' | 'snare' | 'met'; label: string }> = [
      { key: 'kick', label: 'kick' },
      { key: 'snare', label: 'snare' },
      { key: 'met', label: 'metro' },
    ];

    let W = 0;
    let H = 0;
    const ro = new ResizeObserver(() => {
      const dpr = window.devicePixelRatio || 1;
      W = wrap.clientWidth;
      H = 170;
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
            if (!ev[lanes[i].key]) continue;
            const x = (lanes[i].key === 'met' ? posMet : posHit) * W;
            ctx.globalAlpha = alpha;
            ctx.fillStyle = col[lanes[i].key];
            ctx.fillRect(x - 1, i * laneH + 14, 2, laneH - 17);
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
      const errs = evs.filter((e) => e.nudge !== 0).slice(-JITTER_N);
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
        ctx.fillText(`PLL ±${rms.toFixed(1)} ms`, 4, mid);
      } else {
        ctx.fillStyle = col.dim;
        ctx.textBaseline = 'middle';
        ctx.fillText('PLL —', 4, mid);
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
