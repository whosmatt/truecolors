<script lang="ts">
  /*
   * overengineered header banner 
   */
  import { store } from '../lib/state.svelte';

  const rL = $derived(store.scene.r);
  const gL = $derived(store.scene.g);
  const bL = $derived(store.scene.b);
</script>

<div class="beams-bg" aria-hidden="true">
  <svg
    class="beams"
    viewBox="0 0 1200 72"
    preserveAspectRatio="none"
    aria-hidden="true"
    focusable="false"
  >
    <defs>
      <!-- Beer–Lambert exponential fade -->
      <linearGradient id="tc-r" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="1200" y2="0">
        <stop offset="0%" stop-color="#ff2e20" stop-opacity="1" />
        <stop offset="18%" stop-color="#ff2e20" stop-opacity="0.92" />
        <stop offset="44%" stop-color="#ff2e20" stop-opacity="0.72" />
        <stop offset="66%" stop-color="#ff2e20" stop-opacity="0.5" />
        <stop offset="85%" stop-color="#ff2e20" stop-opacity="0.3" />
        <stop offset="94%" stop-color="#ff2e20" stop-opacity="0.14" />
        <stop offset="99%" stop-color="#ff2e20" stop-opacity="0.04" />
        <stop offset="100%" stop-color="#ff2e20" stop-opacity="0" />
      </linearGradient>
      <linearGradient id="tc-g" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="1200" y2="0">
        <stop offset="0%" stop-color="#2dff7d" stop-opacity="1" />
        <stop offset="18%" stop-color="#2dff7d" stop-opacity="0.92" />
        <stop offset="44%" stop-color="#2dff7d" stop-opacity="0.72" />
        <stop offset="66%" stop-color="#2dff7d" stop-opacity="0.5" />
        <stop offset="85%" stop-color="#2dff7d" stop-opacity="0.3" />
        <stop offset="94%" stop-color="#2dff7d" stop-opacity="0.14" />
        <stop offset="99%" stop-color="#2dff7d" stop-opacity="0.04" />
        <stop offset="100%" stop-color="#2dff7d" stop-opacity="0" />
      </linearGradient>
      <linearGradient id="tc-b" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="1200" y2="0">
        <stop offset="0%" stop-color="#3f6bff" stop-opacity="1" />
        <stop offset="18%" stop-color="#3f6bff" stop-opacity="0.92" />
        <stop offset="44%" stop-color="#3f6bff" stop-opacity="0.72" />
        <stop offset="66%" stop-color="#3f6bff" stop-opacity="0.5" />
        <stop offset="85%" stop-color="#3f6bff" stop-opacity="0.3" />
        <stop offset="94%" stop-color="#3f6bff" stop-opacity="0.14" />
        <stop offset="99%" stop-color="#3f6bff" stop-opacity="0.04" />
        <stop offset="100%" stop-color="#3f6bff" stop-opacity="0" />
      </linearGradient>

      <!-- solid white core -->
      <linearGradient id="tc-core" gradientUnits="userSpaceOnUse" x1="0" y1="0" x2="1230" y2="0">
        <stop offset="0%" stop-color="#ffffff" stop-opacity="1" />
        <stop offset="14%" stop-color="#ffffff" stop-opacity="0.92" />
        <stop offset="30%" stop-color="#ffffff" stop-opacity="0.62" />
        <stop offset="48%" stop-color="#ffffff" stop-opacity="0.32" />
        <stop offset="68%" stop-color="#ffffff" stop-opacity="0.1" />
        <stop offset="100%" stop-color="#ffffff" stop-opacity="0" />
      </linearGradient>

      <!-- vertical blur -->
      <filter id="tc-halo" filterUnits="userSpaceOnUse" x="-50" y="-60" width="1320" height="200">
        <feGaussianBlur stdDeviation="3 12" />
      </filter>
      <filter id="tc-corefx" filterUnits="userSpaceOnUse" x="-50" y="-30" width="1320" height="140">
        <feGaussianBlur stdDeviation="0.4" />
      </filter>
    </defs>

    <g style="isolation:isolate">
      <g style="mix-blend-mode:screen; opacity:{rL}">
        <rect x="-30" y="32.7" width="1260" height="3" fill="url(#tc-r)" filter="url(#tc-halo)" />
        <rect x="-30" y="33.6" width="1260" height="1.2" fill="url(#tc-core)" filter="url(#tc-corefx)" />
      </g>
      <g style="mix-blend-mode:screen; opacity:{bL}">
        <rect x="-30" y="36.3" width="1260" height="3" fill="url(#tc-b)" filter="url(#tc-halo)" />
        <rect x="-30" y="37.2" width="1260" height="1.2" fill="url(#tc-core)" filter="url(#tc-corefx)" />
      </g>
      <g style="mix-blend-mode:screen; opacity:{gL}">
        <rect x="-30" y="34.5" width="1260" height="3" fill="url(#tc-g)" filter="url(#tc-halo)" />
        <rect x="-30" y="35.4" width="1260" height="1.2" fill="url(#tc-core)" filter="url(#tc-corefx)" />
      </g>
    </g>
  </svg>

  <!-- Emitter flare, SVG to prevent stretching -->
  <svg
    class="flares"
    viewBox="0 0 130 72"
    preserveAspectRatio="xMinYMid meet"
    aria-hidden="true"
    focusable="false"
  >
    <defs>
      <radialGradient id="tc-flare-r" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stop-color="#ffffff" stop-opacity="1" />
        <stop offset="34%" stop-color="#ff6a55" stop-opacity="0.15" />
        <stop offset="68%" stop-color="#ff2e20" stop-opacity="0.06" />
        <stop offset="100%" stop-color="#ff2e20" stop-opacity="0" />
      </radialGradient>
      <radialGradient id="tc-flare-g" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stop-color="#ffffff" stop-opacity="1" />
        <stop offset="34%" stop-color="#74ffac" stop-opacity="0.15" />
        <stop offset="68%" stop-color="#2dff7d" stop-opacity="0.06" />
        <stop offset="100%" stop-color="#2dff7d" stop-opacity="0" />
      </radialGradient>
      <radialGradient id="tc-flare-b" cx="50%" cy="50%" r="50%">
        <stop offset="0%" stop-color="#ffffff" stop-opacity="1" />
        <stop offset="34%" stop-color="#86a3ff" stop-opacity="0.15" />
        <stop offset="68%" stop-color="#3f6bff" stop-opacity="0.06" />
        <stop offset="100%" stop-color="#3f6bff" stop-opacity="0" />
      </radialGradient>
      <filter id="tc-flarefx" filterUnits="userSpaceOnUse" x="-60" y="-60" width="200" height="200">
        <feGaussianBlur stdDeviation="1.6" />
      </filter>
    </defs>

    <g style="isolation:isolate">
      <g style="mix-blend-mode:screen; opacity:{rL}">
        <ellipse cx="0" cy="34.2" rx="30" ry="18" fill="url(#tc-flare-r)" filter="url(#tc-flarefx)" />
      </g>
      <g style="mix-blend-mode:screen; opacity:{bL}">
        <ellipse cx="0" cy="37.8" rx="30" ry="18" fill="url(#tc-flare-b)" filter="url(#tc-flarefx)" />
      </g>
      <g style="mix-blend-mode:screen; opacity:{gL}">
        <ellipse cx="0" cy="36" rx="30" ry="18" fill="url(#tc-flare-g)" filter="url(#tc-flarefx)" />
      </g>
    </g>
  </svg>
</div>

<h1 class="word">truecolors</h1>

<style>
  .beams-bg {
    position: absolute;
    inset: 0;
    z-index: 0;
    pointer-events: none;
  }
  .beams {
    display: block;
    width: 100%;
    height: 100%;
  }
  .flares {
    position: absolute;
    left: 0;
    top: 0;
    height: 100%;
    width: 130px;
    display: block;
  }

  .word {
    position: relative;
    z-index: 1;
    margin: 0;
    font-size: 2.1rem;
    font-weight: 100;
    letter-spacing: -0.014em;
    line-height: 1;
    white-space: nowrap;
    transform: translateY(-0.08em);
    color: #fff;
    /* dark halo keeps the white text crisp where the beams pass behind it */
    text-shadow:
      0 0 2px rgba(0, 0, 0, 0.9),
      0 0 6px rgba(0, 0, 0, 0.6),
      0 0 14px rgba(0, 0, 0, 0.45),
      0 1px 3px rgba(0, 0, 0, 0.9);
  }
</style>
