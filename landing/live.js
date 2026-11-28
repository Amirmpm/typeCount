/* TypeCount Live — real activity counting for this page.
 * No simulation, no random data, no network. Everything stays in this browser.
 *
 * Counts real user input on THIS page only (a browser cannot see other apps):
 *   - keystrokes (never the content, never inside password fields)
 *   - clicks / taps
 *   - wheel scrolls
 *   - pointer travel (px -> metres)
 * Stores per-day + per-hour buckets in localStorage so the calendar and
 * "last 7 days" are genuinely yours.
 */
(function () {
  "use strict";

  var STORE_KEY = "typecount.live.v1";
  var KEEP_DAYS = 400;
  var IDLE_MS = 5000;      // no input for 5s => not typing anymore
  var WPM_WINDOW = 10000;  // sliding window for WPM
  var PX_TO_M = 0.000264583; // 96 dpi CSS px -> metres

  /* ---------- storage ---------- */
  function pad2(n) { return (n < 10 ? "0" : "") + n; }
  function dateKey(d) {
    d = d || new Date();
    return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate());
  }
  function emptyDay() { return { k: 0, c: 0, s: 0, m: 0, pk: 0, h: {} }; }
  function load() {
    var st = { v: 1, days: {} };
    try {
      var raw = localStorage.getItem(STORE_KEY);
      if (raw) {
        var parsed = JSON.parse(raw);
        if (parsed && parsed.days && typeof parsed.days === "object") st = parsed;
      }
    } catch (e) { /* private mode / disabled storage: run in memory */ }
    if (!st.days) st.days = {};
    return st;
  }

  var state = load();
  var memoryOnly = false;
  try { localStorage.setItem(STORE_KEY + ".probe", "1"); localStorage.removeItem(STORE_KEY + ".probe"); }
  catch (e) { memoryOnly = true; }

  function prune() {
    var cutoff = dateKey(new Date(Date.now() - KEEP_DAYS * 864e5));
    Object.keys(state.days).forEach(function (d) {
      if (d < cutoff) delete state.days[d];
    });
  }

  var saveTimer = null;
  function persist(immediate) {
    if (memoryOnly) return;
    var write = function () {
      saveTimer = null;
      try { localStorage.setItem(STORE_KEY, JSON.stringify(state)); } catch (e) { /* quota */ }
    };
    if (immediate) { if (saveTimer) clearTimeout(saveTimer); write(); return; }
    if (!saveTimer) saveTimer = setTimeout(write, 500);
  }

  /* Any record can come back from an older version, a partial write or manual
     tampering. Coerce it into a complete, well-typed day before use — a missing
     `h` bucket used to throw a TypeError and blank the whole dashboard. */
  function num(v) { return typeof v === "number" && isFinite(v) && v >= 0 ? v : 0; }
  function sanitiseDay(raw) {
    var d = emptyDay();
    if (!raw || typeof raw !== "object") return d;
    d.k = num(raw.k); d.c = num(raw.c); d.s = num(raw.s); d.m = num(raw.m); d.pk = num(raw.pk);
    if (raw.h && typeof raw.h === "object") {
      Object.keys(raw.h).forEach(function (key) {
        var n = parseInt(key, 10);
        if (!(n >= 0 && n < 24)) return;
        var b = raw.h[key];
        if (!b || typeof b !== "object") return;
        d.h[n] = { k: num(b.k), c: num(b.c), s: num(b.s), m: num(b.m) };
      });
    }
    return d;
  }
  /* Read a day, sanitising and repairing it in place on first access. */
  function getDay(key) {
    var raw = state.days[key];
    if (!raw || typeof raw !== "object" || !raw.h || typeof raw.h !== "object") {
      state.days[key] = sanitiseDay(raw);
    }
    return state.days[key];
  }
  function today() {
    var d = dateKey();
    if (!state.days[d]) { state.days[d] = emptyDay(); prune(); }
    return getDay(d);
  }
  function hourOf(day, d) {
    var h = (d || new Date()).getHours();
    if (!day.h || typeof day.h !== "object") day.h = {};
    if (!day.h[h]) day.h[h] = { k: 0, c: 0, s: 0, m: 0 };
    return day.h[h];
  }

  /* ---------- session ---------- */
  var session = { start: Date.now(), k: 0, c: 0, s: 0, m: 0 };
  var keyTimes = [];
  var lastInput = 0;
  var lastWheel = 0;
  var pendingPx = 0;
  var lastMoveAt = 0;

  /* ---------- subscribers / notify ---------- */
  var subs = [];
  var lightSubs = [];
  var notifyQueued = false;
  var lightQueued = false;
  var wasTyping = false;
  var cache = null;
  var lightCache = null;
  function invalidate() { cache = null; lightCache = null; }

  function dispatch(list) {
    var snap = lightCache || snapshot();
    list.forEach(function (fn) { try { fn(snap); } catch (e) { /* ignore */ } });
  }
  function notify() {
    invalidate();
    if (notifyQueued) return;
    notifyQueued = true;
    var run = function () {
      notifyQueued = false;
      var snap = snapshot();
      cache = snap;
      subs.forEach(function (fn) { try { fn(snap); } catch (e) { /* ignore */ } });
    };
    if (window.requestAnimationFrame) requestAnimationFrame(run); else setTimeout(run, 60);
  }
  /* Light refresh: recompute only the cheap live values. Heavier aggregates
     (week/all/hours/series/calendar) are reused from the last full snapshot. */
  function notifyLight() {
    lightCache = null;
    if (lightQueued) return;
    lightQueued = true;
    var run = function () {
      lightQueued = false;
      var snap = liveSnapshot();
      lightSubs.forEach(function (fn) { try { fn(snap); } catch (e) { /* ignore */ } });
    };
    if (window.requestAnimationFrame) requestAnimationFrame(run); else setTimeout(run, 60);
  }

  /* ---------- input capture ---------- */
  var MODIFIERS = /^(Shift|Control|Alt|Meta|AltGraph|CapsLock|NumLock|ScrollLock|Fn|FnLock|Hyper|Super|OS|Tab|Escape|Dead)$/;

  function isSecret(target) {
    var el = target;
    while (el && el.nodeType === 1) {
      var tag = el.tagName;
      if (tag === "INPUT") {
        var t = (el.getAttribute("type") || "text").toLowerCase();
        if (t === "password" || t === "email" || t === "tel") return true;
      }
      if (el.isContentEditable) return false;
      el = el.parentElement;
    }
    return false;
  }

  function recordKey(ev) {
    if (ev.isComposing || !ev.key) return;
    if (MODIFIERS.test(ev.key)) return;
    if (isSecret(ev.target)) return;           // never count secrets
    var now = Date.now();
    var day = today();
    var h = hourOf(day);
    day.k++; h.k++;
    session.k++;
    keyTimes.push(now);
    lastInput = now;
    var w = currentWpm(now);
    if (w > (day.pk || 0)) day.pk = w;
    notify();
    persist(true);
  }

  function recordClick(ev) {
    var now = Date.now();
    var day = today();
    var h = hourOf(day);
    day.c++; h.c++;
    session.c++;
    lastInput = now;
    notify();
    persist(true);
  }

  function recordScroll() {
    var now = Date.now();
    if (now - lastWheel < 120) return;         // one gesture != 50 scrolls
    lastWheel = now;
    var day = today();
    var h = hourOf(day);
    day.s++; h.s++;
    session.s++;
    lastInput = now;
    notify();
    persist();
  }

  function recordMove(dx, dy) {
    var dist = Math.sqrt(dx * dx + dy * dy);
    if (!isFinite(dist) || dist <= 0) return;
    pendingPx += dist;
    lastInput = Date.now();
    if (pendingPx > 400) flushPx();
    notify();
  }
  function flushPx() {
    if (pendingPx <= 0) return;
    var metres = pendingPx * PX_TO_M;
    var day = today();
    var h = hourOf(day);
    day.m += metres; h.m += metres;
    session.m += metres;
    pendingPx = 0;
    persist();
  }

  document.addEventListener("keydown", recordKey, { passive: true, capture: true });
  document.addEventListener("pointerdown", recordClick, { passive: true, capture: true });
  document.addEventListener("wheel", recordScroll, { passive: true, capture: true });

  var lastX = null, lastY = null, moveTimer = null;
  document.addEventListener("pointermove", function (e) {
    if (lastX !== null) recordMove(e.clientX - lastX, e.clientY - lastY);
    lastX = e.clientX; lastY = e.clientY;
    if (!moveTimer) moveTimer = setTimeout(function () { moveTimer = null; flushPx(); }, 900);
  }, { passive: true, capture: true });
  document.addEventListener("pointerleave", function () { lastX = lastY = null; flushPx(); }, { passive: true });

  window.addEventListener("pagehide", function () { flushPx(); persist(true); });
  document.addEventListener("visibilitychange", function () {
    flushPx(); persist(true);
    if (document.visibilityState === "visible") notify();
  });

  /* ---------- derived metrics ---------- */
  /* WPM = (keys in the last 10s / 5) extrapolated to a minute.
     Returns 0 once the user has been idle for IDLE_MS. */
  function currentWpm(now) {
    now = now || Date.now();
    if (!keyTimes.length) return 0;
    var cutoff = now - WPM_WINDOW;
    var i = 0;
    while (i < keyTimes.length && keyTimes[i] < cutoff) i++;
    if (i) keyTimes.splice(0, i);
    if (!keyTimes.length) return 0;
    var last = keyTimes[keyTimes.length - 1];
    if (now - last > IDLE_MS) return 0;
    /* measured span covers the keys, not the idle tail after the last one */
    var span = Math.max(last - keyTimes[0], 1000);
    return (keyTimes.length / 5) * (60000 / span);
  }

  function dayTotals(day) {
    if (!day) return { k: 0, c: 0, s: 0, m: 0 };
    return { k: num(day.k), c: num(day.c), s: num(day.s), m: num(day.m) };
  }
  function rangeTotals(nDays) {
    var out = { k: 0, c: 0, s: 0, m: 0 };
    for (var i = 0; i < nDays; i++) {
      var d = state.days[dateKey(new Date(Date.now() - i * 864e5))];
      if (!d) continue;
      out.k += num(d.k); out.c += num(d.c); out.s += num(d.s); out.m += num(d.m);
    }
    return out;
  }
  function allTotals() {
    var out = { k: 0, c: 0, s: 0, m: 0, days: 0 };
    Object.keys(state.days).forEach(function (key) {
      var d = state.days[key];
      if (!d) return;
      if (num(d.k) || num(d.c) || num(d.s)) out.days++;
      out.k += num(d.k); out.c += num(d.c); out.s += num(d.s); out.m += num(d.m);
    });
    return out;
  }
  function hours24() {
    var day = state.days[dateKey()] ? getDay(dateKey()) : emptyDay();
    var out = [];
    for (var h = 0; h < 24; h++) {
      var b = (day.h && day.h[h]) || { k: 0, c: 0, s: 0 };
      out.push({ hour: h, k: num(b.k), c: num(b.c), s: num(b.s) });
    }
    return out;
  }
  function dailySeries(n) {
    var out = [];
    for (var i = n - 1; i >= 0; i--) {
      var dt = new Date(Date.now() - i * 864e5);
      var d = state.days[dateKey(dt)] || emptyDay();
      out.push({ date: dt, k: num(d.k), c: num(d.c), s: num(d.s), m: num(d.m) });
    }
    return out;
  }
  function calendar(days) {
    days = days || 364;
    var out = [];
    for (var i = days - 1; i >= 0; i--) {
      var dt = new Date(Date.now() - i * 864e5);
      var d = state.days[dateKey(dt)] || emptyDay();
      out.push({ date: dt, k: num(d.k), c: num(d.c), s: num(d.s) });
    }
    return out;
  }
  function streak() {
    var n = 0;
    for (var i = 0; i < KEEP_DAYS; i++) {
      var d = state.days[dateKey(new Date(Date.now() - i * 864e5))];
      var active = d && (num(d.k) > 0 || num(d.c) > 0);
      if (active) n++;
      else if (i === 0) continue;   // today may still be empty
      else break;
    }
    return n;
  }
  function lastHourKeys() {
    var key = dateKey();
    var day = state.days[key] ? getDay(key) : emptyDay();
    var h = new Date().getHours();
    var total = 0;
    [h, (h + 23) % 24].forEach(function (idx, i) {
      var b = day.h && day.h[idx];
      if (!b) return;
      // previous hour only if it is the hour right before now
      if (i === 1 && idx === h) return;
      total += num(b.k);
    });
    return total;
  }

  function snapshot() {
    if (cache) return cache;
    var day = state.days[dateKey()] ? getDay(dateKey()) : emptyDay();
    var now = Date.now();
    var idle = !lastInput || now - lastInput > IDLE_MS;
    var wpm = idle ? 0 : currentWpm(now);
    var d24 = hours24();
    cache = {
      today: dayTotals(day),
      week: rangeTotals(7),
      all: allTotals(),
      hours: d24,
      series7: dailySeries(7),
      series30: dailySeries(30),
      calendar: calendar(364),
      streak: streak(),
      lastHourKeys: lastHourKeys(),
      peakWpm: Math.round(num(day.pk)),
      wpm: wpm,
      cpm: wpm * 5,
      idle: idle,
      session: {
        k: session.k, c: session.c, s: session.s, m: session.m,
        seconds: Math.round((now - session.start) / 1000),
      },
      memoryOnly: memoryOnly,
      storageOK: !memoryOnly,
    };
    return cache;
  }

  /* Cheap snapshot: only re-measures WPM/idle/session/today counters and reuses
     every aggregate from the last full snapshot. */
  function liveSnapshot() {
    if (lightCache) return lightCache;
    var base = cache || snapshot();
    var day = state.days[dateKey()] ? getDay(dateKey()) : emptyDay();
    var now = Date.now();
    var wpm = currentWpm(now);
    var idle = !lastInput || now - lastInput > IDLE_MS;
    lightCache = {
      today: { k: num(day.k), c: num(day.c), s: num(day.s), m: num(day.m) },
      wpm: idle ? 0 : wpm,
      cpm: (idle ? 0 : wpm) * 5,
      idle: idle,
      lastHourKeys: lastHourKeys(),
      streak: base.streak,
      peakWpm: Math.round(num(day.pk)),
      week: base.week,
      all: base.all,
      hours: base.hours,
      series7: base.series7,
      series30: base.series30,
      calendar: base.calendar,
      session: {
        k: session.k, c: session.c, s: session.s, m: session.m,
        seconds: Math.round((now - session.start) / 1000),
      },
      storageOK: !memoryOnly,
      partial: true,
    };
    return lightCache;
  }

  /* ---------- public API ---------- */
  window.TypeCountLive = {
    subscribe: function (fn) { subs.push(fn); fn(snapshot()); return function () { subs = subs.filter(function (f) { return f !== fn; }); }; },
    subscribeLive: function (fn) { lightSubs.push(fn); return function () { lightSubs = lightSubs.filter(function (f) { return f !== fn; }); }; },
    snapshot: snapshot,
    reset: function () {
      state = { v: 1, days: {} };
      session = { start: Date.now(), k: 0, c: 0, s: 0, m: 0 };
      keyTimes = []; pendingPx = 0; lastInput = 0;
      persist(true);
      notify();
    },
    exportJSON: function () { return JSON.stringify({ exportedAt: new Date().toISOString(), days: state.days }, null, 2); },
  };

  /* Normalise everything once, so a single bad record can never break a render. */
  (function repairStore() {
    var keys = Object.keys(state.days);
    for (var i = 0; i < keys.length; i++) {
      var raw = state.days[keys[i]];
      var fixed = sanitiseDay(raw);
      if (!raw || raw.k !== fixed.k || raw.c !== fixed.c || !raw.h) state.days[keys[i]] = fixed;
    }
  })();

  prune();
  notify();

  /* Lightweight ticker: only refreshes live values (WPM/idle/session), never
     rebuilds the chart or the heatmap. Runs at 4 Hz, and drops to zero work
     when the tab is hidden or the user has been idle. */
  setInterval(function () {
    if (document.visibilityState !== "visible") return;
    if (lastInput && Date.now() - lastInput > IDLE_MS && !wasTyping) return;
    wasTyping = !!(lastInput && Date.now() - lastInput <= IDLE_MS);
    notifyLight();
  }, 250);
  window.addEventListener("beforeunload", function () { flushPx(); persist(true); });
})();
