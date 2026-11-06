// Where the end-to-end servers run (playwright.config.ts). 5173 is the `npm run web` address (§15.2);
// VJN6_E2E_PORT / VJN6_E2E_NOISO_PORT move them when those ports are taken by something else.
export const PORT = Number(process.env.VJN6_E2E_PORT ?? 5173);
export const NOISO_PORT = Number(process.env.VJN6_E2E_NOISO_PORT ?? 5174);
export const ORIGIN = `http://localhost:${PORT}`;
export const NOISO_ORIGIN = `http://localhost:${NOISO_PORT}`;
/** The address §15.2 and AC-B2 name. */
export const SPEC_ORIGIN = 'http://localhost:5173';
