<script lang="ts">
  import { slide } from 'svelte/transition';
  import { store } from '../lib/state.svelte';
  import {
    wifiScan,
    wifiProvision,
    wifiAp,
    reboot,
    factoryReset,
    otaUpdate,
    setPwmHz,
  } from '../lib/ws';

  let selectedSsid = $state('');
  let password = $state('');
  let showSettings = $state(false); // collapsible settings

  // confirmation modal for sensitive actions
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

  function forgetNetwork() {
    // Same as connect but with empty credentials
    wifiProvision('', '');
    selectedSsid = '';
    password = '';
  }

  const askAp = () =>
    (pending = {
      title: 'Switch to AP mode',
      body: 'The device will switch to temporary AP mode. If you want persistent AP mode, use \'Forget network\' instead.',
      label: 'Switch to AP',
      run: wifiAp,
    });
  const askForget = () =>
    (pending = {
      title: 'Forget network',
      body: 'The device will disconnect and enter AP mode.',
      label: 'Forget network',
      run: forgetNetwork,
    });
  const askReboot = () =>
    (pending = {
      title: 'Reboot device',
      body: 'Do you want to reboot the device?',
      label: 'Reboot',
      run: reboot,
    });
  const askFactoryReset = () =>
    (pending = {
      title: 'Factory reset',
      body: 'Clear NVS and restore default settings?',
      label: 'Erase & reset',
      danger: true,
      run: factoryReset,
    });

  // ---- Firmware (OTA) update ----
  let otaFile = $state<File | null>(null);
  // null = idle; 0..1 = uploading; 'done' / 'error' once finished.
  let otaProgress = $state<number | 'done' | 'error' | null>(null);
  let otaError = $state('');
  const otaBusy = $derived(typeof otaProgress === 'number');

  function onOtaPick(e: Event) {
    const input = e.target as HTMLInputElement;
    otaFile = input.files?.[0] ?? null;
    otaProgress = null;
    otaError = '';
  }

  function runOta() {
    if (!otaFile) return;
    otaProgress = 0;
    otaError = '';
    otaUpdate(otaFile, {
      onProgress: (f) => (otaProgress = f),
      onDone: () => {
        otaProgress = 'done';
        setTimeout(() => location.reload(), 3000);
      },
      onError: (msg) => {
        otaProgress = 'error';
        otaError = msg;
      },
    });
  }

  function onPwmChange(e: Event) {
    setPwmHz(Number((e.currentTarget as HTMLSelectElement).value));
  }

  const askOta = () => {
    if (!otaFile) return;
    pending = {
      title: 'Flash firmware',
      body: `Upload "${otaFile.name}" (${(otaFile.size / 1024).toFixed(0)} KB)?`,
      label: 'Flash & reboot',
      danger: true,
      run: runOta,
    };
  };
</script>

<svelte:window onkeydown={onKey} />

<div class="card">
  <div class="card-title">Network</div>

  <div class="info">
    <div class="info-row">
      <span class="k">Mode</span>
      <span class="v">{modeLabel}</span>
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

  <button
    class="settings-toggle"
    onclick={() => (showSettings = !showSettings)}
    aria-expanded={showSettings}
  >
    <span class="sub-title">Settings</span>
    <svg
      class="chev"
      class:open={showSettings}
      viewBox="0 0 16 16"
      width="14"
      height="14"
      aria-hidden="true"
    >
      <path
        d="M4 6l4 4 4-4"
        fill="none"
        stroke="currentColor"
        stroke-width="1.6"
        stroke-linecap="round"
        stroke-linejoin="round"
      />
    </svg>
  </button>

  {#if showSettings}
    <div class="settings" transition:slide={{ duration: 200 }}>
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
        <button
          class="btn btn-primary"
          onclick={provision}
          disabled={!selectedSsid}
        >
          Connect
        </button>
      </div>

      <div class="admin">
        <button class="btn" onclick={askAp}>AP Mode</button>
        <button class="btn" onclick={askForget}>Forget Network</button>
        <button class="btn" onclick={askReboot}>Reboot</button>
        <button class="btn btn-danger" onclick={askFactoryReset}>
          Factory Reset
        </button>
      </div>

      <div class="pwm">
        <span class="sub-title">Laser PWM</span>
        <select value={String(store.pwmHz)} onchange={onPwmChange}>
          <option value="120">120 Hz</option>
          <option value="240">240 Hz</option>
          <option value="480">480 Hz</option>
        </select>
      </div>

      <div class="ota">
        <span class="sub-title">Firmware Update</span>
        <div class="ota-pick">
          <label class="btn file-btn">
            {otaFile ? otaFile.name : 'Choose .bin…'}
            <input
              type="file"
              accept=".bin,application/octet-stream"
              onchange={onOtaPick}
            />
          </label>
          <button
            class="btn btn-primary"
            onclick={askOta}
            disabled={!otaFile || otaBusy}
          >
            Flash
          </button>
        </div>

        {#if typeof otaProgress === 'number'}
          <div class="ota-bar"><div class="ota-fill" style="width:{Math.round(otaProgress * 100)}%"></div></div>
          <span class="ota-status">Uploading… {Math.round(otaProgress * 100)}%</span>
        {:else if otaProgress === 'done'}
          <span class="ota-status ok">Upload complete, rebooting…</span>
        {:else if otaProgress === 'error'}
          <span class="ota-status bad">Update failed: {otaError}</span>
        {/if}
      </div>
    </div>
  {/if}
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
  .pwm {
    margin-top: 16px;
    padding-top: 14px;
    border-top: 1px solid var(--border-solid);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 10px;
  }
  .pwm select {
    background: var(--bg-elev-2);
    color: var(--text);
    border: 1px solid var(--border-solid);
    border-radius: var(--radius-sm);
    padding: 7px 10px;
    font-size: 0.85rem;
  }
  /* OTA firmware update */
  .ota {
    margin-top: 16px;
    padding-top: 14px;
    border-top: 1px solid var(--border-solid);
    display: flex;
    flex-direction: column;
    gap: 10px;
  }
  .ota-pick {
    display: flex;
    gap: 8px;
  }
  .file-btn {
    flex: 1 1 auto;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    text-align: center;
    cursor: pointer;
  }
  .file-btn input {
    display: none;
  }
  .ota-pick .btn-primary {
    flex: 0 0 auto;
  }
  .ota-bar {
    height: 6px;
    border-radius: 999px;
    background: var(--bg-elev-2);
    overflow: hidden;
  }
  .ota-fill {
    height: 100%;
    background: var(--accent);
    transition: width 0.15s ease;
  }
  .ota-status {
    font-size: 0.78rem;
    color: var(--text-dim);
  }
  .ota-status.ok {
    color: var(--good);
  }
  .ota-status.bad {
    color: var(--err);
  }
  /* Settings collapse toggle */
  .settings-toggle {
    display: flex;
    align-items: center;
    justify-content: space-between;
    width: 100%;
    padding: 12px 0 2px;
    border: none;
    border-top: 1px solid var(--border-solid);
    background: none;
    color: var(--text-dim);
    transition: color 0.12s ease;
  }
  .settings-toggle:hover {
    color: var(--text);
  }
  .chev {
    transition: transform 0.18s ease;
  }
  .chev.open {
    transform: rotate(180deg);
  }
  .settings {
    margin-top: 14px;
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
