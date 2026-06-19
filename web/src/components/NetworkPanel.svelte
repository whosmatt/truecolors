<script lang="ts">
  import { store } from '../lib/state.svelte';
  import {
    wifiScan,
    wifiProvision,
    wifiAp,
    reboot,
    factoryReset,
  } from '../lib/ws';

  let selectedSsid = $state('');
  let password = $state('');

  // Confirmation modal: each admin action opens it with its own copy + handler.
  interface Confirm {
    title: string;
    body: string;
    label: string;
    danger?: boolean;
    run: () => void;
  }
  let pending = $state<Confirm | null>(null);

  function cancel() {
    pending = null;
  }
  function confirmAction() {
    const p = pending;
    pending = null;
    p?.run();
  }
  function onKey(e: KeyboardEvent) {
    if (e.key === 'Escape') cancel();
  }

  const modeLabel = $derived(
    {
      boot: 'Booting',
      connecting: 'Connecting',
      sta: 'Connected (STA)',
      ap: 'Access Point',
    }[store.net.mode] ?? store.net.mode,
  );

  function authLabel(auth: number): string {
    return auth === 0 ? 'Open' : 'Secured';
  }

  function bars(rssi: number): number {
    if (rssi >= -55) return 4;
    if (rssi >= -67) return 3;
    if (rssi >= -78) return 2;
    return 1;
  }

  function provision() {
    if (!selectedSsid) return;
    wifiProvision(selectedSsid, password);
    password = '';
  }

  const askAp = () =>
    (pending = {
      title: 'Switch to AP mode',
      body: 'The device leaves your network and broadcasts its own Wi-Fi for setup. It reconnects to the saved network on the next reboot.',
      label: 'Switch to AP',
      run: wifiAp,
    });
  const askReboot = () =>
    (pending = {
      title: 'Reboot device',
      body: 'The device restarts and will be unavailable for a few seconds.',
      label: 'Reboot',
      run: reboot,
    });
  const askFactoryReset = () =>
    (pending = {
      title: 'Factory reset',
      body: 'Erases saved Wi-Fi credentials and the saved scene, then reboots into AP mode. This cannot be undone.',
      label: 'Erase & reset',
      danger: true,
      run: factoryReset,
    });
</script>

<svelte:window onkeydown={onKey} />

<div class="card">
  <div class="card-title">Network</div>

  <div class="info">
    <div class="info-row">
      <span class="k">Mode</span>
      <span class="v"
        ><span class="mode-dot" data-mode={store.net.mode}></span>{modeLabel}</span
      >
    </div>
    <div class="info-row">
      <span class="k">SSID</span><span class="v">{store.net.ssid || '—'}</span>
    </div>
    <div class="info-row">
      <span class="k">IP</span><span class="v mono">{store.net.ip || '—'}</span>
    </div>
    <div class="info-row">
      <span class="k">Hostname</span><span class="v mono"
        >{store.net.hostname || '—'}</span
      >
    </div>
  </div>

  <div class="scan-head">
    <span class="sub-title">Available Networks</span>
    <button class="btn" onclick={wifiScan} disabled={store.scanning}>
      {store.scanning ? 'Scanning…' : 'Scan'}
    </button>
  </div>

  {#if store.aps.length > 0}
    <div class="ap-list">
      {#each store.aps as ap (ap.ssid + ap.rssi)}
        <button
          class="ap"
          class:sel={selectedSsid === ap.ssid}
          onclick={() => (selectedSsid = ap.ssid)}
        >
          <span class="ap-name">{ap.ssid}</span>
          <span class="ap-meta">
            <span class="auth">{authLabel(ap.auth)}</span>
            <span class="bars" aria-label="{ap.rssi} dBm">
              {#each [1, 2, 3, 4] as b (b)}
                <i class:on={b <= bars(ap.rssi)}></i>
              {/each}
            </span>
          </span>
        </button>
      {/each}
    </div>
  {/if}

  <div class="provision">
    <input
      type="text"
      placeholder="SSID"
      bind:value={selectedSsid}
      autocomplete="off"
    />
    <input
      type="password"
      placeholder="Password"
      bind:value={password}
      autocomplete="off"
    />
    <button class="btn btn-primary" onclick={provision} disabled={!selectedSsid}>
      Connect
    </button>
  </div>

  <div class="admin">
    <button class="btn" onclick={askAp}>AP Mode</button>
    <button class="btn" onclick={askReboot}>Reboot</button>
    <button class="btn btn-danger" onclick={askFactoryReset}>Factory Reset</button>
  </div>
</div>

{#if pending}
  <!-- Backdrop click is a mouse convenience; Esc and the Cancel button give
       keyboard users an equivalent path. -->
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
  <div class="modal-overlay" role="presentation" onclick={cancel}>
    <div
      class="modal"
      role="dialog"
      aria-modal="true"
      aria-label={pending.title}
      onclick={(e) => e.stopPropagation()}
    >
      <h3>{pending.title}</h3>
      <p>{pending.body}</p>
      <div class="modal-actions">
        <button class="btn" onclick={cancel}>Cancel</button>
        <button
          class="btn {pending.danger ? 'btn-danger' : 'btn-primary'}"
          onclick={confirmAction}
        >
          {pending.label}
        </button>
      </div>
    </div>
  </div>
{/if}

<style>
  .info {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-bottom: 16px;
  }
  .info-row {
    display: flex;
    justify-content: space-between;
    font-size: 0.85rem;
  }
  .k {
    color: var(--text-dim);
  }
  .v {
    color: var(--text);
    display: inline-flex;
    align-items: center;
    gap: 7px;
  }
  .mono {
    font-family: var(--mono);
    font-size: 0.82rem;
  }
  .mode-dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: var(--text-faint);
  }
  .mode-dot[data-mode='sta'] {
    background: var(--good);
    box-shadow: 0 0 8px var(--good);
  }
  .mode-dot[data-mode='ap'] {
    background: var(--accent-2);
    box-shadow: 0 0 8px var(--accent-2);
  }
  .mode-dot[data-mode='connecting'] {
    background: var(--warn);
  }
  .scan-head {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 10px;
  }
  .sub-title {
    font-size: 0.74rem;
    text-transform: uppercase;
    letter-spacing: 0.07em;
    color: var(--text-faint);
    font-weight: 600;
  }
  .ap-list {
    display: flex;
    flex-direction: column;
    gap: 4px;
    margin-bottom: 14px;
    max-height: 190px;
    overflow-y: auto;
  }
  .ap {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 9px 11px;
    border-radius: var(--radius-sm);
    border: 1px solid transparent;
    background: var(--bg-elev-2);
    color: var(--text);
    text-align: left;
  }
  .ap:hover {
    background: #23232f;
  }
  .ap.sel {
    border-color: var(--accent);
    background: rgba(124, 92, 255, 0.12);
  }
  .ap-name {
    font-size: 0.86rem;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .ap-meta {
    display: inline-flex;
    align-items: center;
    gap: 10px;
    flex: 0 0 auto;
  }
  .auth {
    font-size: 0.72rem;
    color: var(--text-faint);
  }
  .bars {
    display: inline-flex;
    align-items: flex-end;
    gap: 2px;
    height: 14px;
  }
  .bars i {
    width: 3px;
    background: var(--text-faint);
    border-radius: 1px;
    opacity: 0.35;
  }
  .bars i:nth-child(1) {
    height: 5px;
  }
  .bars i:nth-child(2) {
    height: 8px;
  }
  .bars i:nth-child(3) {
    height: 11px;
  }
  .bars i:nth-child(4) {
    height: 14px;
  }
  .bars i.on {
    opacity: 1;
    background: var(--accent-2);
  }
  .provision {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-bottom: 14px;
  }
  .admin {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
  }
  .admin .btn {
    flex: 1 1 auto;
  }
  .modal-overlay {
    position: fixed;
    inset: 0;
    z-index: 100;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 20px;
    background: rgba(0, 0, 0, 0.6);
    backdrop-filter: blur(3px);
  }
  .modal {
    width: 100%;
    max-width: 380px;
    background: var(--bg-elev-1, #16161f);
    border: 1px solid var(--border-solid, rgba(255, 255, 255, 0.12));
    border-radius: var(--radius, 14px);
    padding: 22px;
    box-shadow: 0 24px 60px -12px rgba(0, 0, 0, 0.7);
  }
  .modal h3 {
    margin: 0 0 10px;
    font-size: 1.02rem;
  }
  .modal p {
    margin: 0 0 20px;
    font-size: 0.88rem;
    line-height: 1.5;
    color: var(--text-dim);
  }
  .modal-actions {
    display: flex;
    justify-content: flex-end;
    gap: 10px;
  }
</style>
