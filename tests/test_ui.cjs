'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');

const uiDir = path.join(__dirname, '..', 'ui');
const locales = JSON.parse(fs.readFileSync(path.join(uiDir, 'locales.json'), 'utf8'));
const createI18n = require(path.join(uiDir, 'i18n.js'));
const H = require(path.join(uiDir, 'history.js'));
const K = require(path.join(uiDir, 'keyboard.js'));
const C = require(path.join(uiDir, 'shortcuts-catalog.js'));
const T = require(path.join(uiDir, 'themes.js'));

// 1. Locale completeness (en vs fa)
const enKeys = Object.keys(locales.en);
const faKeys = Object.keys(locales.fa);
assert(enKeys.length >= 500, 'Expected full locale catalog');
for (const k of enKeys) {
  assert(typeof locales.fa[k] === 'string' && locales.fa[k].length > 0, `Missing fa translation for ${k}`);
  assert(!locales.en[k].includes('ClickPulse'), `Found ClickPulse in en[${k}]`);
  assert(!locales.fa[k].includes('ClickPulse'), `Found ClickPulse in fa[${k}]`);
}

// 2. i18n formatting & RTL check for English and Persian
const i18nEn = createI18n(locales, 'en', 'windows');
assert.strictEqual(i18nEn.locale, 'en');
assert.strictEqual(i18nEn.isRtl, false);
assert.strictEqual(i18nEn.t('page.overview'), 'Overview');
assert.strictEqual(i18nEn.t('stealth.notice', { hotkey: 'Ctrl+Alt+Shift+T' }).includes('Ctrl+Alt+Shift+T'), true);

const i18nFa = createI18n(locales, 'fa', 'windows');
assert.strictEqual(i18nFa.locale, 'fa');
assert.strictEqual(i18nFa.isRtl, true);
assert.strictEqual(i18nFa.t('page.overview'), 'نمای کلی');
assert.strictEqual(i18nFa.t('page.apps'), 'برنامه‌ها');

// 3. Themes, Keyboard, History, Shortcuts catalog sanity
for (const name of ['blue', 'teal', 'violet', 'graphite']) {
  const th = T.get(name);
  assert(th && th.accent && th.mouse, `Theme ${name} missing fields`);
}
const kdist = K.distribution({ physical: [{ code: 0, count: 100 }, { code: 49, count: 250 }] }, 'history', 'compact');
assert(kdist.total === 350, 'Keyboard distribution total mismatch');
const parts = C.chordParts({ modifiers: 1, key: 8 });
assert.deepStrictEqual(parts, ['Ctrl', 'C']);
const scale = H.countScale(1250);
assert(scale.ceiling >= 1250 && scale.ticks.length >= 2);

// 4. index.html required sections & asset integrity
const html = fs.readFileSync(path.join(uiDir, 'index.html'), 'utf8');
assert(!html.includes('ClickPulse'), 'Found leftover ClickPulse string in index.html');
for (const id of [
  'overview', 'history', 'keys', 'apps', 'shortcuts', 'share', 'settings',
  'mini-widget', 'close-overlay', 'tc-live-wpm', 'tc-goal-fill', 'tc-apps-list',
  'tc-stealth-enter', 'tc-wake-hotkey-select', 'tc-startup-mode-select',
  'tc-export-csv', 'tc-export-json', 'tc-backup-now', 'language-select'
]) {
  assert(html.includes(`id="${id}"`), `Missing element #${id} in index.html`);
}
for (const src of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
  if (src[1].startsWith('http://') || src[1].startsWith('https://')) continue;
  assert(fs.existsSync(path.join(uiDir, src[1])), `Referenced UI asset missing: ${src[1]}`);
}

console.log(`All Node UI & i18n checks passed (${enKeys.length} en keys, ${faKeys.length} fa keys)!`);
