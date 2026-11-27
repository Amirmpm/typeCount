'use strict';
const assert = require('assert');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

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

// 4. index.html required sections, version 1.0.1 & asset integrity
const html = fs.readFileSync(path.join(uiDir, 'index.html'), 'utf8');
assert(!html.includes('ClickPulse'), 'Found leftover ClickPulse string in index.html');
assert(html.includes('>1.0.1<'), 'Expected sidebar version 1.0.1 in index.html');
for (const id of [
  'overview', 'history', 'keys', 'apps', 'shortcuts', 'share', 'settings',
  'mini-widget', 'close-overlay', 'tc-live-wpm', 'tc-goal-fill', 'tc-apps-list',
  'tc-stealth-enter', 'tc-wake-hotkey-select', 'tc-startup-mode-select',
  'tc-export-csv', 'tc-export-json', 'tc-backup-now', 'language-select',
  'window-minimize', 'window-maximize', 'window-close'
]) {
  assert(html.includes(`id="${id}"`), `Missing element #${id} in index.html`);
}
for (const src of html.matchAll(/(?:href|src)="([^"]+)"/g)) {
  if (src[1].startsWith('http://') || src[1].startsWith('https://')) continue;
  assert(fs.existsSync(path.join(uiDir, src[1])), `Referenced UI asset missing: ${src[1]}`);
}

// 5. Execute all index.html <script> files in exact order in a DOM/WebView2 mock and test window controls + IPC
const scriptMatches = [...html.matchAll(/<script\s+src="([^"]+)"><\/script>/g)].map(m => m[1]);
assert(scriptMatches.indexOf('app.js') < scriptMatches.indexOf('presentation.js'), 'app.js must load before presentation.js');
assert(scriptMatches.indexOf('presentation.js') < scriptMatches.indexOf('typecount-ext.js'), 'presentation.js must load before typecount-ext.js');

class MockClassList {
  constructor() { this.set = new Set(); }
  add(...c) { c.forEach(x => this.set.add(x)); }
  remove(...c) { c.forEach(x => this.set.delete(x)); }
  toggle(c, f) {
    if (f === undefined) { if (this.set.has(c)) { this.set.delete(c); return false; } this.set.add(c); return true; }
    if (f) this.set.add(c); else this.set.delete(c);
    return !!f;
  }
  contains(c) { return this.set.has(c); }
}
class MockNode {
  constructor(tag = 'div', id = '') {
    this.tagName = tag.toUpperCase();
    this.id = id;
    this.children = [];
    this.childNodes = this.children;
    this.attributes = {};
    this.dataset = {};
    this.style = { setProperty() {}, removeProperty() {} };
    this.classList = new MockClassList();
    this.listeners = new Map();
    this.hidden = false;
    this.disabled = false;
    this.checked = false;
    this.value = '';
    this.options = [];
    this.textContent = '';
    this.parentNode = null;
    this.parentElement = null;
  }
  get firstElementChild() { return this.children[0] || null; }
  get lastElementChild() { return this.children[this.children.length - 1] || null; }
  get firstChild() { return null; }
  setAttribute(k, v) { this.attributes[k] = String(v); if (k === 'id') this.id = String(v); }
  getAttribute(k) { return this.attributes[k] ?? null; }
  removeAttribute(k) { delete this.attributes[k]; }
  append(...nodes) {
    for (const n of nodes) {
      if (typeof n === 'string') continue;
      n.parentNode = this; n.parentElement = this;
      this.children.push(n);
    }
  }
  replaceChildren(...nodes) { this.children = []; this.childNodes = this.children; this.append(...nodes); }
  insertBefore(n) { n.parentNode = this; n.parentElement = this; }
  addEventListener(ev, fn) { if (!this.listeners.has(ev)) this.listeners.set(ev, []); this.listeners.get(ev).push(fn); }
  dispatchEvent(ev) {
    ev.target = ev.target || this;
    ev.preventDefault = ev.preventDefault || (() => {});
    ev.stopPropagation = ev.stopPropagation || (() => {});
    for (const fn of (this.listeners.get(ev.type) || [])) fn(ev);
  }
  click() { this.dispatchEvent({ type: 'click', button: 0 }); }
  focus() {}
  scrollIntoView() {}
  closest() { return null; }
  contains() { return false; }
  getBoundingClientRect() { return { left: 0, top: 0, right: 200, bottom: 40, width: 200, height: 40 }; }
  getContext() { return null; }
}

const byId = new Map();
const makeEl = (id, tag = 'div', opts = []) => {
  if (!byId.has(id)) {
    const el = new MockNode(tag, id);
    if (tag === 'select') {
      el.parentNode = new MockNode('div');
      el.parentElement = el.parentNode;
      el.options = opts.map(v => {
        const o = new MockNode('option'); o.value = v; o.textContent = v; return o;
      });
      if (opts.length) el.value = opts[0];
    }
    byId.set(id, el);
  }
  return byId.get(id);
};

// Seed DOM elements referenced by scripts
for (const id of [
  'sidebar-toggle', 'sidebar-toggle-arrow', 'toolbar-title', 'page-title', 'app-sidebar',
  'project-home', 'star-notice', 'star-dismiss', 'star-open', 'demo-badge',
  'tc-stealth-header-btn', 'compact', 'window-minimize', 'window-maximize',
  'window-maximize-glyph', 'window-close', 'permission-banner', 'permission',
  'permission-title', 'permission-updated-title', 'permission-reset',
  'error-banner', 'data-operation-status', 'data-operation-message',
  'today', 'today-detail', 'today-label', 'week', 'week-detail', 'lifetime', 'lifetime-detail',
  'tc-peak-wpm', 'tc-live-wpm', 'tc-live-cpm', 'tc-today-scroll', 'tc-streak-pill',
  'tc-goal-pct', 'tc-goal-fill', 'tc-goal-text', 'tc-break-card', 'tc-break-text',
  'tc-break-hint', 'tc-break-reset', 'tc-today-distance', 'tc-life-distance',
  'windows', 'pause', 'hourly-range', 'hourly-total', 'hourly-chart', 'hourly-detail',
  'trend-range', 'trend-chart', 'donut', 'composition-legend', 'calendar-aside',
  'active-days', 'heatmap', 'calendar-range', 'history-range', 'history-summary',
  'history-chart', 'scatter-title', 'scatter-weekdays', 'scatter-keyboard', 'scatter-mouse',
  'scatter-plot', 'scatter-tip', 'scatter-status', 'scatter-keys', 'scatter-range',
  'scatter-track', 'scatter-peaks', 'scatter-from', 'scatter-to', 'scatter-period',
  'history-table', 'history-page-info', 'history-prev', 'history-next',
  'keyboard-layout-mode', 'keyboard-compact', 'keyboard-full', 'keyboard-view-toggle',
  'keyboard-blocks-icon', 'keyboard-layout-icon', 'keyboard-view-label', 'keyboard-layout',
  'keyboard-treemap', 'mouse-diagram', 'mouse-chart', 'mouse-distribution',
  'tc-focus-clock', 'tc-focus-btn', 'tc-apps-today-btn', 'tc-apps-life-btn',
  'tc-apps-list', 'tc-apps-empty', 'shortcut-loading', 'shortcut-dashboard',
  'shortcut-atlas', 'shortcut-ranking', 'shortcut-details', 'shortcut-details-title',
  'shortcut-search', 'shortcut-list', 'shortcut-more', 'share-workspace', 'share-start-field',
  'share-start-label', 'share-start', 'share-start-trigger', 'share-start-value',
  'share-date-label', 'share-date-caption', 'share-date', 'share-date-trigger',
  'share-date-value', 'share-save', 'share-copy', 'share-prompt-panel', 'share-prompt-text',
  'share-prompt-copy', 'share-prompt-regenerate', 'share-deck', 'share-card-prev',
  'share-card-next', 'share-prev-name', 'share-next-name', 'share-stage', 'share-canvas',
  'share-deck-prev', 'share-deck-next', 'share-deck-dots', 'share-empty',
  'personalization-settings', 'sport-visible', 'tc-daily-goal-input', 'tc-dpi-input',
  'handoff-settings', 'handoff-description', 'takeover', 'capture-settings',
  'permission-row', 'permission-description', 'permission-reset-settings',
  'settings-permission', 'ignore-software-row', 'ignore-software-input', 'tc-track-apps-switch',
  'tc-stealth-enter', 'storage-settings', 'db-description', 'db-path', 'storage-directory',
  'reveal', 'tc-export-csv', 'tc-export-json', 'tc-backup-now', 'tc-auto-backup-switch',
  'import-history-row', 'import-history', 'app-settings', 'timezone-row', 'update-row',
  'update-status', 'update-managed', 'update-install-help', 'update-last-checked',
  'update-details', 'check-updates', 'automatic-updates', 'update-automatic-unavailable',
  'autostart-row', 'autostart', 'close-action-row', 'send-feedback', 'uninstall-row',
  'uninstall', 'settings-nav', 'settings-subnav', 'mini-widget', 'sport-canvas',
  'mini-sport-layer', 'mini-drag', 'mini-sport', 'mini-pin', 'mini-hide', 'mini-expand',
  'mini-period-label', 'mini-today', 'mini-status', 'mini-sport-readout', 'mini-sport-grade',
  'mini-sport-cps', 'mini-keyboard', 'mini-mouse', 'mini-pause', 'mini-pause-glyph',
  'mini-pause-label', 'close-overlay', 'close-cancel', 'close-remember', 'close-hide',
  'close-stealth', 'close-quit', 'toast'
]) makeEl(id);

makeEl('theme-select', 'select', ['blue', 'teal', 'violet', 'graphite']);
makeEl('ui-scale-select', 'select', ['80', '90', '100', '115', '130', '150']);
makeEl('language-select', 'select', ['en', 'fa', 'system']);
makeEl('today-period-select', 'select', ['calendar_day', 'rolling_24h']);
makeEl('timezone-select', 'select', ['system', 'Asia/Tehran', 'UTC']);
makeEl('close-action-select', 'select', ['ask', 'hide', 'stealth', 'quit']);
makeEl('calendar-range-select', 'select', ['365']);
makeEl('history-range-select', 'select', ['all']);
makeEl('share-style-select', 'select', ['blue', 'paper', 'ink', 'pine', 'clay', 'lavender', 'rose']);
makeEl('tc-break-interval-select', 'select', ['0', '20', '45', '60']);
makeEl('tc-startup-mode-select', 'select', ['tray', 'stealth']);
makeEl('tc-wake-hotkey-select', 'select', ['Ctrl+Alt+Shift+T', 'None']);
makeEl('keyboard-layout').parentElement = new MockNode('div');

const headerEl = new MockNode('header');
const mainEl = new MockNode('main');
const sidebarEl = makeEl('app-sidebar', 'aside');
const bodyEl = new MockNode('body');
const docEl = new MockNode('html');
docEl.dir = 'ltr'; docEl.lang = 'en';
const sentIpc = [];
const sandbox = {
  console, setTimeout, clearTimeout, setInterval, clearInterval,
  Intl, Math, Date, JSON, Array, Object, String, Number, Boolean, Map, Set, RegExp, Error, Promise,
  URL, URLSearchParams,
  Path2D: function () {},
  ResizeObserver: function () { return { observe() {}, disconnect() {} }; },
  requestAnimationFrame: fn => fn(),
  addEventListener: () => {},
  removeEventListener: () => {},
  matchMedia: () => ({ matches: false }),
  innerWidth: 1200,
  innerHeight: 800,
  devicePixelRatio: 1,
  chrome: { webview: { postMessage: raw => sentIpc.push(JSON.parse(raw)) } },
  navigator: { language: 'en-US' },
  document: {
    readyState: 'complete',
    documentElement: docEl,
    body: bodyEl,
    activeElement: null,
    getElementById: id => byId.get(id) || makeEl(id),
    createElement: tag => new MockNode(tag),
    createElementNS: (_ns, tag) => new MockNode(tag),
    querySelector: sel => sel === 'header' ? headerEl : sel === 'main' ? mainEl : sel === '.sidebar' ? sidebarEl : null,
    querySelectorAll: sel => {
      if (sel === 'select') return [...byId.values()].filter(e => e.tagName === 'SELECT');
      return [];
    },
    addEventListener: (ev, fn) => bodyEl.addEventListener(ev, fn)
  }
};
sandbox.window = sandbox;
vm.createContext(sandbox);
for (const file of scriptMatches) {
  const code = fs.readFileSync(path.join(uiDir, file), 'utf8');
  vm.runInContext(code, sandbox, { filename: file });
}

// Verify all critical window functions registered & 'ready' fired cleanly AFTER presentation.js
assert.strictEqual(typeof sandbox.receiveView, 'function', 'receiveView must be defined');
assert.strictEqual(typeof sandbox.receive, 'function', 'receive must be defined');
assert.strictEqual(typeof sandbox.requestClose, 'function', 'requestClose must be defined');
assert(sentIpc.some(m => m.action === 'ready'), 'ready IPC must be sent on boot');

// Verify Minimize, Maximize, Close, Compact, Stealth buttons dispatch exact IPC actions
sentIpc.length = 0;
byId.get('window-minimize').click();
byId.get('window-maximize').click();
byId.get('window-close').click();
byId.get('compact').click();
byId.get('tc-stealth-header-btn').click();
assert.deepStrictEqual(sentIpc.map(m => m.action), ['minimize', 'maximize', 'close', 'compact', 'enter_stealth']);

// Verify Close modal buttons work too
sentIpc.length = 0;
sandbox.requestClose();
assert.strictEqual(byId.get('close-overlay').hidden, false);
byId.get('close-stealth').click();
assert.deepStrictEqual(sentIpc, [{ action: 'close_decision', close_action: 'stealth', remember: false }]);

// Verify receiveView switches to Persian RTL cleanly
sandbox.receiveView({
  locale: 'fa',
  language_preference: 'fa',
  theme: 'teal',
  ui_scale: '100',
  compact: false,
  maximized: true,
  close_action: 'ask',
  timezones: ['UTC', 'Asia/Tehran']
});
assert.strictEqual(docEl.dir, 'rtl', 'Persian locale must switch html dir to rtl');
assert.strictEqual(byId.get('window-maximize-glyph').attributes.d, 'M5 3h8v8M3 5h8v8H3Z');

// Verify window.receive(snapshot) renders overview cleanly without throwing
sandbox.receive({
  now: '2026-11-27T12:00:00',
  timezone: 'system',
  today_period: 'calendar_day',
  paused: false,
  demo: false,
  auto_start: true,
  auto_start_available: true,
  database_path: 'C:\\Users\\Test\\AppData\\Local\\TypeCount\\typecount.sqlite3',
  database_mode: 'default',
  capture: { listening: true, permission_needed: false, error: null, error_code: null },
  today: { keyboard: 1200, mouse: 350 },
  week: { keyboard: 8400, mouse: 2100 },
  lifetime: { keyboard: 42000, mouse: 12000 },
  windows: [
    { seconds: 60, approximate: false, counts: { keyboard: 25, mouse: 5 } },
    { seconds: 600, approximate: true, counts: { keyboard: 200, mouse: 45 } },
    { seconds: 3600, approximate: true, counts: { keyboard: 800, mouse: 180 } },
    { seconds: 86400, approximate: true, counts: { keyboard: 1200, mouse: 350 } }
  ],
  hourly: Array.from({ length: 24 }, (_, i) => ({
    start_ms: Date.now() - (23 - i) * 3600000,
    end_ms: Date.now() - (22 - i) * 3600000,
    approximate: false,
    counts: { keyboard: i * 10, mouse: i * 3 }
  })),
  hours: Array.from({ length: 24 }, () => ({ keyboard: 50, mouse: 15 })),
  days: [{ day: '2026-11-27', counts: { keyboard: 1200, mouse: 350 } }],
  history_days: [{ day: '2026-11-27', counts: { keyboard: 1200, mouse: 350 } }],
  keyboard_stats: { physical: [{ code: 0, count: 400 }, { code: 49, count: 600 }] },
  mouse_buttons: { left: 250, right: 80, middle: 20, side_back: 0, side_forward: 0, other: 0 },
  shortcuts: [{ platform: 'windows', modifiers: 1, key: 8, count: 42 }],
  apps_today: [{ app: 'Code.exe', keyboard: 900, mouse: 200, scroll: 60, active_sec: 1200 }],
  apps_lifetime: [{ app: 'Code.exe', keyboard: 9000, mouse: 2000, scroll: 600, active_sec: 12000 }],
  live_wpm: 62.5,
  live_cpm: 312.5,
  live_cps: 3.0,
  today_peak_wpm: 88.0,
  today_scroll: 95,
  today_distance_m: 12.4,
  lifetime_distance_m: 340.2,
  streak_days: 5,
  goal_target: 8000,
  break_elapsed_sec: 600,
  break_due: false
});
assert.strictEqual(byId.get('tc-live-wpm').textContent, '62.5');
assert.strictEqual(byId.get('tc-today-distance').textContent, '12.4');

console.log(`All Node UI, DOM script execution, window controls & i18n checks passed (${enKeys.length} en keys, ${faKeys.length} fa keys)!`);
