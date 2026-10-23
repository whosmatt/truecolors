<script lang="ts">
  import { slide } from 'svelte/transition';
  import { store } from '../lib/state.svelte';
  import PressConfirm from './PressConfirm.svelte';
  import {
    wifiScan,
    addNetwork,
    forgetNetwork,
    wifiAp,
    reboot,
    factoryReset,
    otaUpdate,
    setPwmHz,
    setEpilepsySafe,
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

  let showForget = $state(false); // saved-network list

  function cancel() {
    pending = null;
    showForget = false;
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

  function add() {
    if (!selectedSsid) return;
    addNetwork(selectedSsid, password);
    password = '';
  }

  function forget(ssid: string) {
    showForget = false;
    forgetNetwork(ssid);
  }

  const askAp = () =>
    (pending = {
      title: 'Switch to AP mode',
      body: 'Enable AP until a network is added or the device is restarted',
      label: 'Switch to AP',
      run: wifiAp,
    });
  const askReboot = () =>
    (pending = {
      title: 'Reboot device',
      body: 'Do you want to reboot the device?',
      label: 'Reboot',
      run: reboot,
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

  const otaPhase = $derived.by(() => {
    if (otaProgress !== 1) return null;
    return store.ota?.stage === 'write' ? 'write' : 'verify';
  });
  const otaBar = $derived(
    otaPhase === 'write' ? (store.ota?.progress ?? 0) : typeof otaProgress === 'number' ? otaProgress : 0,
  );

  function runOta() {
    if (!otaFile) return;
    store.ota = null;
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
                {#if store.net.known.includes(ap.ssid)}
                  <span class="saved">Saved</span>
                {/if}
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
          onclick={add}
          disabled={!selectedSsid}
        >
          Add network
        </button>
      </div>

      <div class="admin">
        <button class="btn" onclick={askAp}>AP Mode</button>
        <button
          class="btn"
          onclick={() => (showForget = true)}
          disabled={store.net.known.length === 0}
        >
          Forget Network
        </button>
        <button class="btn" onclick={askReboot}>Reboot</button>
        <button class="btn btn-danger" onclick={factoryReset}>
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

      <div class="pwm">
        <span class="sub-title">Epilepsy Safe</span>
        <label class="switch">
          <input
            type="checkbox"
            checked={store.epilepsySafe}
            onchange={(e) =>
              setEpilepsySafe((e.currentTarget as HTMLInputElement).checked)}
            aria-label="Epilepsy safe mode"
          />
          <span class="knob"></span>
        </label>
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
          <div class="ota-bar"><div class="ota-fill" style="width:{Math.round(otaBar * 100)}%"></div></div>
          <span class="ota-status">
            {#if otaPhase === 'write'}
              Writing to flash… {Math.round(otaBar * 100)}%
            {:else if otaPhase === 'verify'}
              Verifying signature…
            {:else}
              Uploading… {Math.round(otaBar * 100)}%
            {/if}
          </span>
        {:else if otaProgress === 'done'}
          <span class="ota-status ok">Update complete, rebooting…</span>
        {:else if otaProgress === 'error'}
          <span class="ota-status bad">Update failed: {otaError}</span>
        {/if}
      </div>
    </div>
  {/if}
</div>

<PressConfirm />

{#if showForget}
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
  <div class="modal-overlay" role="presentation" onclick={cancel}>
    <div
      class="modal"
      role="dialog"
      aria-modal="true"
      aria-label="Forget network"
      onclick={(e) => e.stopPropagation()}
    >
      <h3>Forget network</h3>
      <div class="known">
        {#each store.net.known as ssid (ssid)}
          <div class="known-row">
            <span class="ap-name">{ssid}</span>
            {#if ssid === store.net.ssid && store.net.mode === 'sta'}
              <span class="auth">connected</span>
            {/if}
            <button class="btn btn-danger" onclick={() => forget(ssid)}>Forget</button>
          </div>
        {:else}
          <span class="auth">No saved networks.</span>
        {/each}
      </div>
      <div class="modal-actions">
        <button class="btn" onclick={cancel}>Close</button>
      </div>
    </div>
  </div>
{/if}

{#if pending}
  <!-- Backdrop click or esc also cancel -->
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
  .saved {
    font-size: 0.72rem;
    color: var(--accent-2);
  }
  .known {
    display: flex;
    flex-direction: column;
    gap: 6px;
    margin-bottom: 20px;
  }
  .known-row {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 6px 6px 6px 11px;
    border-radius: var(--radius-sm);
    background: var(--bg-elev-2);
  }
  .known-row .ap-name {
    flex: 1 1 auto;
    min-width: 0;
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
  .switch {
    position: relative;
    display: inline-block;
    width: 40px;
    height: 22px;
    flex: 0 0 auto;
  }
  .switch input {
    position: absolute;
    opacity: 0;
    width: 100%;
    height: 100%;
    margin: 0;
    cursor: pointer;
  }
  .switch .knob {
    position: absolute;
    inset: 0;
    border-radius: 999px;
    background: var(--bg-elev-2);
    border: 1px solid var(--border-solid);
    transition: background 0.15s ease;
    pointer-events: none;
  }
  .switch .knob::after {
    content: '';
    position: absolute;
    top: 2px;
    left: 2px;
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background: var(--text-dim);
    transition:
      transform 0.15s ease,
      background 0.15s ease;
  }
  .switch input:checked + .knob {
    background: var(--accent);
    border-color: var(--accent);
  }
  .switch input:checked + .knob::after {
    transform: translateX(18px);
    background: #fff;
  }
  .switch input:focus-visible + .knob {
    outline: 2px solid var(--accent-2);
    outline-offset: 2px;
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
</style>
