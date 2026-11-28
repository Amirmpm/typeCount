#!/usr/bin/env node
/**
 * assets-version.mjs — content-hash cache busting for the landing page.
 *
 * Appends `?v=<first 10 chars of sha256>` to the CSS/JS URLs referenced by
 * landing/index.html. Because the query string changes whenever the file
 * content changes, a deploy immediately produces new URLs and no browser cache
 * — not even a previously-set long max-age — can serve the old code.
 *
 * Safe to run repeatedly: hashes are recomputed from the current file contents.
 *
 * Usage:
 *   node scripts/assets-version.mjs          # rewrite landing/index.html
 *   node scripts/assets-version.mjs --check  # exit 1 if versioning is stale
 */
import { createHash } from "node:crypto";
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const LANDING = join(ROOT, "landing");
const HTML_PATH = join(LANDING, "index.html");

/** Files whose URL needs a content hash. */
const VERSIONED = ["style.css", "live.js", "app.js"];

const checkOnly = process.argv.includes("--check");

function hashOf(file) {
  return createHash("sha256").update(readFileSync(join(LANDING, file))).digest("hex").slice(0, 10);
}

const hashes = Object.fromEntries(VERSIONED.map((f) => [f, hashOf(f)]));
const original = readFileSync(HTML_PATH, "utf8");
let html = original;
let changes = 0;

for (const [file, hash] of Object.entries(hashes)) {
  // Match `/file`, `file`, optionally already carrying ?v=...
  const escaped = file.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
  const re = new RegExp(`(href|src)="\\/?${escaped}(?:\\?v=[0-9a-f]+)?"`, "g");
  html = html.replace(re, (match, attr) => {
    const next = `${attr}="/${file}?v=${hash}"`;
    if (next !== match) changes++;
    return next;
  });
}

if (checkOnly) {
  if (changes > 0) {
    console.error(
      `✘ landing/index.html has stale asset versions (${changes} change${changes === 1 ? "" : "s"} needed).\n` +
        "  Run: node scripts/assets-version.mjs"
    );
    process.exit(1);
  }
  console.log("✔ asset versions are current");
  for (const [f, h] of Object.entries(hashes)) console.log(`  ${f} → ${h}`);
  process.exit(0);
}

if (changes === 0) {
  console.log("✔ asset versions already current, no changes written");
  for (const [f, h] of Object.entries(hashes)) console.log(`  ${f} → ${h}`);
} else {
  writeFileSync(HTML_PATH, html);
  console.log(`✔ versioned ${changes} reference${changes === 1 ? "" : "s"} in landing/index.html`);
  for (const [f, h] of Object.entries(hashes)) console.log(`  ${f} → ${h}`);
}
