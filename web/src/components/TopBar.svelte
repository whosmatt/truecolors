<script lang="ts">
  import Logo from './Logo.svelte';
</script>

<header class="topbar">
  <Logo />
</header>

<style>
  /* see Logo.svelte */
  .topbar {
    position: relative;
    z-index: 0;
    overflow: hidden;
    min-height: 72px;
    display: flex;
    align-items: center;
    justify-content: center;
  }
</style>
