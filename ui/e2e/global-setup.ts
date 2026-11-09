// Refuses to run against a port that serves another app: locally Playwright reuses a server that is
// already listening, and that server must be this VJN6 checkout's Vite dev server.
import { NOISO_PORT, PORT } from './env';

/** The page at `port`, tried on every loopback name (localhost may resolve to ::1 or 127.0.0.1). */
async function fetchPage(port: number): Promise<string> {
  const errors: string[] = [];
  for (const host of ['localhost', '127.0.0.1', '[::1]']) {
    try {
      return await (await fetch(`http://${host}:${port}/`)).text();
    } catch (e) {
      errors.push(`${host}: ${String(e)}`);
    }
  }
  throw new Error(`http://localhost:${port} does not answer (${errors.join('; ')})`);
}

async function assertVjn6(port: number): Promise<void> {
  const html = await fetchPage(port);
  if (!html.includes('<title>VJN6</title>') || !html.includes('/src/main.ts')) {
    throw new Error(
      `http://localhost:${port} serves another app, not the VJN6 dev server. Stop it, or move the e2e ` +
        'servers with VJN6_E2E_PORT / VJN6_E2E_NOISO_PORT.',
    );
  }
}

export default async function globalSetup(): Promise<void> {
  await assertVjn6(PORT);
  await assertVjn6(NOISO_PORT);
}
