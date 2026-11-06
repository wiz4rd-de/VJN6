/// <reference types="vitest/config" />
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// §15.2: the page must be cross-origin isolated (SharedArrayBuffer for the Wasm audio worklet),
// both from the dev server (`npm run web`) and from `vite preview`.
const isolationHeaders = {
  'Cross-Origin-Opener-Policy': 'same-origin',
  'Cross-Origin-Embedder-Policy': 'require-corp',
};

export default defineConfig({
  // One bundle for every build (§9.4): the desktop WebView serves ui/dist from its own resource
  // provider, so every asset URL is relative.
  base: './',
  plugins: [svelte()],
  server: {
    host: 'localhost',
    port: 5173,
    strictPort: true,
    headers: isolationHeaders,
  },
  preview: {
    host: 'localhost',
    port: 5173,
    strictPort: true,
    headers: isolationHeaders,
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    // Never inline assets as data: URIs (AC-SC3); fonts stay separate woff2 files.
    assetsInlineLimit: 0,
    target: 'es2022',
  },
  test: {
    include: ['tests/**/*.test.ts'],
    environment: 'node',
  },
});
