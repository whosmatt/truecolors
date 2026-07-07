<script lang="ts">
  import { store } from '../lib/state.svelte';

  const m = $derived(store.metrics);

  const connTitle = $derived(
    store.conn === 'open'
      ? 'Connected'
      : store.conn === 'connecting'
        ? 'Connecting…'
        : 'Offline',
  );

  // Map a metric to the warn/err flags that affect it, for highlighting.
  const FLAG_METRIC: Record<string, string> = {
    vin_low: 'vin',
    undercurrent: 'pdCurrent',
    pd_lost: 'pdOk',
    overtemp: 'laserTemp',
    fan_stall: 'fanRpm',
  };

  // Human labels for the flags.
  const FLAG_LABEL: Record<string, string> = {
    vin_low: 'VIN low',
    undercurrent: 'Undercurrent',
    pd_lost: 'PD lost',
    overtemp: 'Overtemp',
    fan_stall: 'Fan stall',
  };

  // Severity for a given metric key based on active flags.
  function severity(key: string): 'err' | 'warn' | '' {
    if (!m) return '';
    for (const f of m.err) if (FLAG_METRIC[f] === key) return 'err';
    for (const f of m.warn) if (FLAG_METRIC[f] === key) return 'warn';
    return '';
  }

  const rows = $derived(
    m
      ? [
          { key: 'vin', label: 'Input Voltage', val: `${m.vin.toFixed(1)} V` },
          {
            key: 'laserTemp',
            label: 'Laser Temp',
            val: `${m.laserTemp.toFixed(1)} °C`,
          },
          {
            key: 'mcuTemp',
            label: 'MCU Temp',
            val: `${m.mcuTemp.toFixed(0)} °C`,
          },
          { key: 'fanRpm', label: 'Fan Speed', val: `${m.fanRpm} rpm` },
          {
            key: 'fanDuty',
            label: 'Fan Duty',
            val: `${Math.round(m.fanDuty * 100)}%`,
          },
          {
            key: 'pdCurrent',
            label: 'PD Current',
            val: `${m.pdCurrent.toFixed(2)} A`,
          },
          { key: 'pdOk', label: 'PD Status', val: m.pdOk ? 'OK' : 'Fault' },
        ]
      : [],
  );
</script>

<div class="card metrics">
  <div class="metrics-head">
    <div class="card-title" style="margin:0">Telemetry</div>
    <span
      class="conn"
      data-state={store.conn}
      title={connTitle}
      aria-label={connTitle}
      role="status"
    ></span>
  </div>

  {#if !m}
    <p class="empty">No telemetry yet…</p>
  {:else}
    <div class="rows">
      {#each rows as r (r.key)}
        {@const sev = severity(r.key)}
        <div class="metric" class:err={sev === 'err'} class:warn={sev === 'warn'}>
          <span class="m-label">{r.label}</span>
          <span class="m-val" class:bad={r.key === 'pdOk' && !m.pdOk}>{r.val}</span>
        </div>
      {/each}
    </div>

    {#if m.err.length || m.warn.length}
      <div class="flags">
        {#each m.err as f (f)}
          <span class="badge badge-err">{FLAG_LABEL[f] ?? f}</span>
        {/each}
        {#each m.warn as f (f)}
          <span class="badge badge-warn">{FLAG_LABEL[f] ?? f}</span>
        {/each}
      </div>
    {/if}
  {/if}
</div>

<style>
  .metrics-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 14px;
  }
  .conn {
    width: 9px;
    height: 9px;
    border-radius: 50%;
    background: var(--text-faint);
    flex: 0 0 auto;
  }
  .conn[data-state='open'] {
    background: var(--good);
    box-shadow: 0 0 9px var(--good);
  }
  .conn[data-state='connecting'] {
    background: var(--warn);
    animation: pulse 1.2s ease-in-out infinite;
  }
  .conn[data-state='closed'] {
    background: var(--err);
    box-shadow: 0 0 9px var(--err);
  }
  @keyframes pulse {
    0%,
    100% {
      opacity: 0.35;
    }
    50% {
      opacity: 1;
    }
  }
  .rows {
    display: flex;
    flex-direction: column;
  }
  .metric {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 9px 2px;
    border-bottom: 1px solid rgba(255, 255, 255, 0.05);
  }
  .metric:last-child {
    border-bottom: none;
  }
  .m-label {
    font-size: 0.84rem;
    color: var(--text-dim);
  }
  .m-val {
    font-family: var(--mono);
    font-size: 0.88rem;
    font-variant-numeric: tabular-nums;
    color: var(--text);
  }
  .metric.warn .m-label,
  .metric.warn .m-val {
    color: var(--warn);
    font-weight: 700;
  }
  .metric.err .m-label,
  .metric.err .m-val {
    color: var(--err);
    font-weight: 700;
  }
  .m-val.bad {
    color: var(--err);
    font-weight: 700;
  }
  .flags {
    display: flex;
    flex-wrap: wrap;
    gap: 7px;
    margin-top: 14px;
  }
  .badge {
    font-size: 0.74rem;
    font-weight: 600;
    padding: 4px 9px;
    border-radius: 999px;
    border: 1px solid transparent;
  }
  .badge-err {
    background: rgba(255, 77, 94, 0.16);
    color: var(--err);
    border-color: rgba(255, 77, 94, 0.45);
  }
  .badge-warn {
    background: rgba(245, 166, 35, 0.16);
    color: var(--warn);
    border-color: rgba(245, 166, 35, 0.45);
  }
  .empty {
    color: var(--text-faint);
    font-size: 0.85rem;
    margin: 6px 0 0;
  }
</style>
