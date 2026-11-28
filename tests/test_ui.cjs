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
const J = require(path.join(uiDir, 'jalali.js'));

// 0. Jalali calendar roundtrip & Persian leap year checks
const j1 = J.toJalali(2026, 11, 27);
assert.deepStrictEqual(j1, { jy: 1405, jm: 9, jd: 6 });
assert.deepStrictEqual(J.toGregorian(1405, 9, 6), { gy: 2026, gm: 11, gd: 27 });
assert.deepStrictEqual(J.toJalali(2025, 3, 20), { jy: 1403, jm: 12, jd: 30 }); // leap year 1403 Esfand 30
assert.deepStrictEqual(J.toGregorian(1403, 12, 30), { gy: 2025, gm: 3, gd: 20 });
const jFmtFa = J.formatJalaliDate(new Date('2026-11-27T12:00:00Z'), { year: 'numeric', month: 'long', day: 'numeric', timeZone: 'UTC' }, 'fa', new Intl.NumberFormat('fa-IR', { useGrouping: false }));
assert(jFmtFa.includes('آذر') && jFmtFa.includes('۱۴۰۵'), `Expected Persian Azar 1405, got ${jFmtFa}`);
const jFmtEn = J.formatJalaliDate(new Date('2026-11-27T12:00:00Z'), { year: 'numeric', month: 'short', day: 'numeric', timeZone: 'UTC' }, 'en');
assert(jFmtEn.includes('Azar') && jFmtEn.includes('1405'), `Expected Azar 1405, got ${jFmtEn}`);

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

// 3. Themes (Light + Dark), Keyboard, History, Shortcuts catalog sanity
for (const name of ['blue', 'teal', 'violet', 'graphite']) {
  const thLight = T.get(name, false);
  const thDark = T.get(name, true);
  assert(thLight && thLight.accent && thLight.mouse, `Theme ${name} light missing fields`);
  assert(thDark && thDark.accent && thDark.dark === true && thDark.background !== thLight.background, `Theme ${name} dark missing fields`);
}
assert.strictEqual(T.resolveDark('dark', false), true);
assert.strictEqual(T.resolveDark('light', true), false);
assert.strictEqual(T.resolveDark('system', true), true);
const kdist = K.distribution({ physical: [{ code: 0, count: 100 }, { code: 49, count: 250 }] }, 'history', 'compact');
assert(kdist.total === 350, 'Keyboard distribution total mismatch');
const parts = C.chordParts({ modifiers: 1, key: 8 });
assert.deepStrictEqual(parts, ['Ctrl', 'C']);
const scale = H.countScale(1250);
assert(scale.ceiling >= 1250 && scale.ticks.length >= 2);

// 4. index.html required sections, version 1.2.0 & asset integrity
const html = fs.readFileSync(path.join(uiDir, 'index.html'), 'utf8');
assert(!html.includes('ClickPulse'), 'Found leftover ClickPulse string in index.html');
assert(html.includes('>1.2.0<'), 'Expected sidebar version 1.2.0 in index.html');
assert(html.includes('jalali.js'), 'index.html must load jalali.js');
for (const id of [
  'overview', 'history', 'keys', 'apps', 'typingtest', 'typingtest-nav', 'shortcuts', 'share', 'settings',
  'mini-widget', 'close-overlay', 'tc-live-wpm', 'tc-goal-fill', 'tc-apps-list',
  'tc-ergo-card', 'tc-exclusions-card', 'tc-exclude-input', 'tc-exclude-add',
  'tc-tt-passage', 'tc-tt-input', 'tc-tt-restart', 'tc-tt-history',
  'tc-stealth-enter', 'tc-wake-hotkey-select', 'tc-startup-mode-select',
  'tc-typing-test-switch', 'tc-typing-test-stats-switch', 'tc-ergo-insights-switch',
  'tc-ergo-guard-switch', 'tc-smart-alerts-switch', 'tc-sound-alerts-switch',
  'tc-sound-select', 'tc-sound-preset-row', 'tc-sound-preview',
  'color-mode-select', 'tc-dpi-auto-switch', 'tc-dpi-detect-btn', 'tc-dpi-detected', 'tc-dpi-row',
  'tc-app-exclusions-switch', 'tc-pause-excluded-switch', 'tc-idle-detection-switch',
  'tc-idle-timeout-select', 'tc-jalali-switch', 'sport-enabled', 'automatic-updates',
  'reset-storage-directory', 'tc-export-csv', 'tc-export-json', 'tc-backup-now', 'language-select',
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
  select() {}
  scrollIntoView() {}
  closest() { return null; }
  contains() { return false; }
  querySelector() { return null; }
  querySelectorAll() { return []; }
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
  'typingtest', 'typingtest-nav', 'tc-ergo-card', 'tc-focus-score', 'tc-active-time',
  'tc-peak-hour', 'tc-strain-badge', 'tc-strain-ops',
  'tc-exclusions-card', 'tc-exclude-input', 'tc-exclude-add', 'tc-excluded-list', 'tc-excluded-empty',
  'tc-tt-wpm', 'tc-tt-acc', 'tc-tt-err', 'tc-tt-timer-label', 'tc-tt-best',
  'tc-tt-passage', 'tc-tt-input', 'tc-tt-restart', 'tc-tt-history', 'tc-tt-empty', 'tc-tt-hint', 'tc-tt-stage',
  'tc-typing-test-switch', 'tc-typing-test-stats-switch', 'tc-ergo-insights-switch',
  'tc-ergo-guard-switch', 'tc-smart-alerts-switch', 'tc-sound-alerts-switch',
  'tc-app-exclusions-switch', 'tc-pause-excluded-switch', 'tc-idle-detection-switch',
  'tc-idle-timeout-row', 'tc-jalali-switch', 'sport-enabled', 'reset-storage-directory',
  'share-start-label', 'share-start', 'share-start-trigger', 'share-start-value',
  'share-date-label', 'share-date-caption', 'share-date', 'share-date-trigger',
  'share-date-value', 'share-save', 'share-copy', 'share-prompt-panel', 'share-prompt-text',
  'share-prompt-copy', 'share-prompt-regenerate', 'share-deck', 'share-card-prev',
  'share-card-next', 'share-prev-name', 'share-next-name', 'share-stage', 'share-canvas',
  'share-deck-prev', 'share-deck-next', 'share-deck-dots', 'share-empty',
  'personalization-settings', 'sport-visible', 'tc-daily-goal-input', 'tc-dpi-input',
  'tc-dpi-auto-switch', 'tc-dpi-detect-btn', 'tc-dpi-detected', 'tc-dpi-row',
  'tc-sound-preset-row', 'tc-sound-preview',
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
makeEl('color-mode-select', 'select', ['system', 'light', 'dark']);
makeEl('tc-sound-select', 'select', ['pastel_pearl', 'mist_chime', 'peach_marimba', 'lavender_harp', 'mint_dew', 'silk_breeze', 'none']);
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
makeEl('tc-idle-timeout-select', 'select', ['30', '60', '120', '300']);
makeEl('tc-tt-lang', 'select', ['en', 'fa', 'code']);
makeEl('tc-tt-dur', 'select', ['15', '30', '60']);
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

// Verify Jalali toggle & Persian date formatting via I.day
sandbox.receiveView({
  locale: 'fa',
  language_preference: 'fa',
  jalali_calendar: true,
  theme: 'blue',
  ui_scale: '100',
  compact: false,
  maximized: false,
  close_action: 'ask',
  timezones: ['UTC', 'Asia/Tehran']
});
const faJalaliDay = vm.runInContext("I.day('2026-11-27')", sandbox);
assert(faJalaliDay.includes('آذر') && faJalaliDay.includes('۱۴۰۵'), `Expected Jalali Azar 1405 via I.day, got ${faJalaliDay}`);

// Verify v1.1.0 settings toggles emit exact IPC messages
for (const [elId, expectedAction] of [
  ['tc-typing-test-switch', 'typing_test_enabled'],
  ['tc-typing-test-stats-switch', 'typing_test_count_stats'],
  ['tc-ergo-insights-switch', 'ergo_insights_enabled'],
  ['tc-ergo-guard-switch', 'ergo_guard_enabled'],
  ['tc-smart-alerts-switch', 'smart_alerts_enabled'],
  ['tc-sound-alerts-switch', 'sound_alerts_enabled'],
  ['tc-app-exclusions-switch', 'app_exclusions_enabled'],
  ['tc-pause-excluded-switch', 'pause_in_excluded_apps'],
  ['tc-idle-detection-switch', 'idle_detection_enabled'],
  ['tc-jalali-switch', 'jalali_calendar'],
  ['sport-enabled', 'sport'],
  ['automatic-updates', 'automatic_updates']
]) {
  sentIpc.length = 0;
  const sw = byId.get(elId);
  sw.checked = !sw.checked;
  sw.dispatchEvent({ type: 'change', target: sw });
  assert(sentIpc.some(m => m.action === expectedAction), `Changing #${elId} must emit ${expectedAction}`);
}

// Verify focus-safe input protection on #tc-daily-goal-input
const goalInput = byId.get('tc-daily-goal-input');
goalInput.value = '12345';
sandbox.document.activeElement = goalInput;
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
  windows: [], hourly: [], hours: [], days: [], history_days: [],
  keyboard_stats: { physical: [] }, mouse_buttons: {}, shortcuts: [],
  apps_today: [], apps_lifetime: [],
  daily_goal: 8000, goal_target: 8000, dpi_ppi: 96,
  focus_score: 86, today_active_sec: 1400, peak_hour: 12, ergo_10m_ops: 280
});
assert.strictEqual(goalInput.value, '12345', 'Focused input value must not be overwritten during live snapshot');
sandbox.document.activeElement = null;

console.log(`All Node UI, Jalali, v1.1.0 toggles, DOM script execution, window controls & i18n checks passed (${enKeys.length} en keys, ${faKeys.length} fa keys)!`);
