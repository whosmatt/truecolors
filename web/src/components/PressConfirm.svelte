<script lang="ts">
  import { store, type PressAction } from '../lib/state.svelte';
  import { cancelConfirm } from '../lib/ws';

  const press = $derived(store.press);
  let now = $state(performance.now());

  $effect(() => {
    if (press?.state !== 'armed') return;
    const t = setInterval(() => (now = performance.now()), 250);
    return () => clearInterval(t);
  });

  // Briefly show result
  $effect(() => {
    const p = press;
    if (!p || p.state === 'armed') return;
    const t = setTimeout(() => {
      if (store.press === p) store.press = null;
    }, 3000);
    return () => clearTimeout(t);
  });

  const secondsLeft = $derived(
    press?.deadline ? Math.max(0, Math.ceil((press.deadline - now) / 1000)) : 0,
  );

  const titles: Record<PressAction, string> = {
    factory_reset: 'Factory reset',
    add_network: 'Add network',
    forget_network: 'Forget network',
  };

  function what(action?: PressAction, ssid?: string): string {
    switch (action) {
      case 'factory_reset':
        return 'Erases all settings and saved networks';
      case 'add_network':
        return `Saves “${ssid}” as a known network.`;
      case 'forget_network':
        return `Removes “${ssid}” from the known networks.`;
      default:
        return '';
    }
  }

  function outcome(): string {
    if (!press) return '';
    switch (press.state) {
      case 'confirmed':
        return press.action === 'factory_reset'
          ? 'Confirmed. The device is resetting.'
          : press.action === 'add_network'
            ? 'Network saved'
            : 'Network forgotten';
      case 'expired':
        return 'Timed out';
      case 'cancelled':
        return 'Cancelled';
      case 'busy':
        return 'Another action is pending';
      case 'rejected':
        return press.reason === 'full'
          ? 'The network list is full. Forget a network first.'
          : press.reason === 'unknown'
            ? 'This should not happen: press reason unknown'
            : 'Malformed network name or password';
      default:
        return '';
    }
  }

  function close() {
    if (press?.state === 'armed') cancelConfirm();
    else store.press = null;
  }
  function onKey(e: KeyboardEvent) {
    if (press && e.key === 'Escape') close();
  }
</script>

<svelte:window onkeydown={onKey} />

{#if press}
  <!-- Backdrop click closes an outcome only; an armed action needs Cancel or Esc. -->
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
  <div
    class="modal-overlay"
    role="presentation"
    onclick={() => press.state !== 'armed' && close()}
  >
    <div
      class="modal"
      role="dialog"
      aria-modal="true"
      aria-label={press.action ? titles[press.action] : 'Device confirmation'}
      onclick={(e) => e.stopPropagation()}
    >
      <h3>{press.action ? titles[press.action] : 'Not started'}</h3>
      {#if press.state === 'armed'}
        <p>{what(press.action, press.ssid)}</p>
        <div class="press">
          <span class="blink" aria-hidden="true"></span>
          <span>
            Waiting for confirmation<br />
            <span class="dim">Confirm within {secondsLeft} s</span>
          </span>
        </div>
        <div class="modal-actions">
          <button class="btn" onclick={close}>Cancel</button>
        </div>
      {:else}
        <p class:ok={press.state === 'confirmed'}>{outcome()}</p>
        <div class="modal-actions">
          <button class="btn" onclick={close}>Close</button>
        </div>
      {/if}
    </div>
  </div>
{/if}

<style>
  .press {
    display: flex;
    align-items: center;
    gap: 14px;
    margin: 0 0 20px;
    padding: 12px 14px;
    border-radius: var(--radius-sm);
    background: var(--bg-elev-2);
    font-size: 0.88rem;
    line-height: 1.45;
  }
  .dim {
    color: var(--text-dim);
    font-size: 0.8rem;
  }
  .blink {
    flex: 0 0 auto;
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: #fff;
    box-shadow: 0 0 10px rgba(255, 255, 255, 0.8);
    animation: blink 0.5s steps(1, end) infinite;
  }
  @keyframes blink {
    50% {
      opacity: 0;
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .blink {
      animation: none;
    }
  }
  .ok {
    color: var(--good);
  }
</style>
