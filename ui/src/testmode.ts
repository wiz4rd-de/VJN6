// Test builds (§10): the page exposes window.__vjn6Test when it is served with `?test` or built in
// Vite's `test` mode. Nothing else changes.
export function isTestMode(): boolean {
  return (
    import.meta.env.MODE === 'test' ||
    (typeof location !== 'undefined' && new URLSearchParams(location.search).has('test'))
  );
}
