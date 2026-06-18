import { defineConfig, loadEnv } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';

// The device host is configurable via VITE_DEVICE_HOST (see .env.example).
// In dev, /ws and /api are proxied to the live firmware so the UI iterates
// without reflashing. In production everything is served from the device root
// using relative paths.
export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  const deviceHost = env.VITE_DEVICE_HOST || 'truecolors.local';

  return {
    plugins: [svelte(), viteSingleFile()],
    // Relative base so the bundle works when served from the device root.
    base: './',
    server: {
      proxy: {
        '/ws': {
          target: `ws://${deviceHost}`,
          ws: true,
          changeOrigin: true,
        },
        '/api': {
          target: `http://${deviceHost}`,
          changeOrigin: true,
        },
      },
    },
    build: {
      // viteSingleFile inlines everything; these keep the bundle clean.
      cssCodeSplit: false,
      assetsInlineLimit: 100000000,
      chunkSizeWarningLimit: 100000000,
    },
  };
});
