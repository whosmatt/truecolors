// Cross-platform gzip of the built single-file SPA using Node's zlib.
// Produces dist/index.html.gz (what the firmware embeds) and prints sizes.
import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { gzipSync, constants } from 'node:zlib';
import { resolve, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const input = resolve(root, 'dist', 'index.html');
const output = resolve(root, 'dist', 'index.html.gz');

if (!existsSync(input)) {
  console.error(`gzip: ${input} not found — run "vite build" first.`);
  process.exit(1);
}

const raw = readFileSync(input);
const compressed = gzipSync(raw, { level: constants.Z_BEST_COMPRESSION });
writeFileSync(output, compressed);

/** @param {number} n */
const kib = (n) => (n / 1024).toFixed(1);
const ratio = ((1 - compressed.length / raw.length) * 100).toFixed(1);
console.log(
  `gzip: index.html ${raw.length} B (${kib(raw.length)} KiB) -> ` +
    `index.html.gz ${compressed.length} B (${kib(compressed.length)} KiB), ${ratio}% smaller`,
);
