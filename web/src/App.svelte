<script lang="ts">
  import { onMount } from 'svelte';
  import { connect, disconnect } from './lib/ws';
  import { store } from './lib/state.svelte';
  import TopBar from './components/TopBar.svelte';
  import Controls from './components/Controls.svelte';
  import CieChart from './components/CieChart.svelte';
  import RgbSliders from './components/RgbSliders.svelte';
  import HsvSliders from './components/HsvSliders.svelte';
  import EffectPanel from './components/EffectPanel.svelte';
  import MetricsSidebar from './components/MetricsSidebar.svelte';
  import NetworkPanel from './components/NetworkPanel.svelte';

  onMount(() => {
    connect();
    return () => disconnect();
  });

  // Dim the whole control surface when the device is powered off.
  const dim = $derived(!store.scene.power);
</script>

<TopBar />

<div class="app">
  <main class="grid">
    <section class="col col-color" class:dim>
      <CieChart />
      <div class="color-sliders">
        <RgbSliders />
        <HsvSliders />
      </div>
    </section>

    <section class="col col-mid">
      <Controls />
      <div class:dim>
        <EffectPanel />
      </div>
    </section>

    <aside class="col col-side">
      <MetricsSidebar />
      <NetworkPanel />
    </aside>
  </main>

  <footer class="foot">
    whosmatt · truecolors
  </footer>
</div>

<style>
  .app {
    max-width: 1280px;
    margin: 0 auto;
    padding: 0 18px 18px;
    display: flex;
    flex-direction: column;
    gap: 18px;
  }
  .grid {
    display: grid;
    grid-template-columns: minmax(320px, 1.3fr) minmax(240px, 1fr) minmax(
        260px,
        0.9fr
      );
    gap: 18px;
    align-items: start;
  }
  .col {
    display: flex;
    flex-direction: column;
    gap: 18px;
    min-width: 0;
  }
  .color-sliders {
    display: flex;
    flex-direction: column;
    gap: 18px;
  }
  /* Powered-off controls fade but stay interactive (so you can preset). */
  .dim {
    opacity: 0.6;
    transition: opacity 0.25s ease;
  }
  .foot {
    text-align: center;
    color: var(--text-faint);
    font-size: 0.76rem;
    padding: 6px 0 14px;
  }

  @media (max-width: 1040px) {
    .grid {
      grid-template-columns: 1fr 1fr;
    }
    .col-side {
      grid-column: 1 / -1;
      flex-direction: row;
      flex-wrap: wrap;
    }
    .col-side > :global(*) {
      flex: 1 1 280px;
    }
  }
  @media (max-width: 720px) {
    .app {
      padding: 0 12px 12px;
      gap: 14px;
    }
    .grid {
      grid-template-columns: 1fr;
      gap: 14px;
    }
    .col {
      gap: 14px;
    }
  }
</style>
