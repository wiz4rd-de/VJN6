// AC-SC3: ui/dist holds no raster files and no `data:image/` raster URIs (§5.1, §9.5: no raster assets
// in the UI). Node-side: the bundle is built from the current sources first (`vite build`).
import { execFileSync } from 'node:child_process';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, extname, join, relative } from 'node:path';
import { fileURLToPath } from 'node:url';
import { expect, test } from '@playwright/test';

const UI = join(dirname(fileURLToPath(import.meta.url)), '..');
const DIST = join(UI, 'dist');
const RASTER_EXT = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.ico', '.avif', '.tif', '.tiff']);
// any data:image/ URI except vector SVG, also in its URL-encoded form (data:image%2Fpng)
const DATA_IMAGE = /data:image(?:\/|%2f)(?!svg\+xml|svg%2bxml)[a-z0-9.+%-]+/gi;
const TEXT_EXT = new Set(['.html', '.js', '.mjs', '.css', '.json', '.svg', '.map', '.txt', '.webmanifest']);

function walk(dir: string): string[] {
  return readdirSync(dir).flatMap((n) => {
    const p = join(dir, n);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

test('AC-SC3 ui/dist contains no png/jpg/gif/webp/bmp files and no data:image/ raster URIs', () => {
  const vite = join(dirname(createRequire(import.meta.url).resolve('vite/package.json')), 'bin', 'vite.js');
  execFileSync(process.execPath, [vite, 'build', '--logLevel', 'error'], {
    cwd: UI,
    stdio: 'pipe',
  });
  const files = walk(DIST);
  expect(files.some((f) => f.endsWith('index.html')), 'the bundle was built').toBe(true);

  const raster = files.filter((f) => RASTER_EXT.has(extname(f).toLowerCase())).map((f) => relative(DIST, f));
  expect(raster, 'raster files in ui/dist').toEqual([]);

  const uris: string[] = [];
  for (const f of files) {
    if (!TEXT_EXT.has(extname(f).toLowerCase())) continue;
    for (const m of readFileSync(f, 'utf8').matchAll(DATA_IMAGE)) uris.push(`${relative(DIST, f)}: ${m[0]}`);
  }
  expect(uris, 'data:image/ raster URIs in ui/dist').toEqual([]);
});
