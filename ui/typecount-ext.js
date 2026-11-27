'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const t = (key, vals = {}) => window.ClickPulseI18n ? window.ClickPulseI18n.t(key, vals) : key;
  const n = val => window.ClickPulseI18n ? window.ClickPulseI18n.number(val) : String(val || 0);
  const d = (val, digits = 1) => window.ClickPulseI18n ? window.ClickPulseI18n.decimal(val, digits) : String(val || 0);
  const send = (action, values = {}) => window.ipc?.postMessage(JSON.stringify({ action, ...values }));

  let appsPeriod = 'today';
  let focusActive = false;
  let focusEndMs = 0;
  let focusStartCounts = 0;
  let focusTimerHandle = null;

  function fmtDuration(sec) {
    sec = Math.max(0, Math.floor(sec || 0));
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  }

  window.renderTypeCountOverview = snapshot => {
    if (!snapshot) return;
    const wpmEl = $('tc-live-wpm'), cpmEl = $('tc-live-cpm'), peakEl = $('tc-peak-wpm');
    if (wpmEl) wpmEl.textContent = d(snapshot.live_wpm || 0, 1);
    if (cpmEl) cpmEl.textContent = `${n(Math.round(snapshot.live_cpm || 0))} ${t('speed.cpm_unit')}`;
    if (peakEl) peakEl.textContent = t('speed.peak_today', { wpm: d(snapshot.today_peak_wpm || 0, 1) });

    const scrollEl = $('tc-today-scroll'), distEl = $('tc-today-distance'), lifeDistEl = $('tc-life-distance');
    if (scrollEl) scrollEl.textContent = t('speed.scrolls', { count: n(snapshot.today_scroll || 0) });
    if (distEl) distEl.textContent = d(snapshot.today_distance_m || 0, 1);
    if (lifeDistEl) lifeDistEl.textContent = `${t('metric.lifetime')}: ${d(snapshot.lifetime_distance_m || 0, 1)} m`;

    const goalTarget = Math.max(1, Number(snapshot.goal_target || window.presentationView?.daily_goal || 8000));
    const summary = window.counterSummary ? window.counterSummary(snapshot) : { counts: snapshot.today };
    const todayTotal = ((summary?.counts?.keyboard || 0) + (summary?.counts?.mouse || 0));
    const pct = Math.min(100, (todayTotal / goalTarget) * 100);
    const goalTextEl = $('tc-goal-text'), goalPctEl = $('tc-goal-pct'), goalFill = $('tc-goal-fill'), streakEl = $('tc-streak-pill');
    if (goalTextEl) goalTextEl.textContent = t('goal.progress', { current: n(todayTotal), target: n(goalTarget), percent: d(pct, 0) });
    if (goalPctEl) goalPctEl.textContent = pct >= 100 ? t('goal.reached') : `${d(pct, 0)}%`;
    if (goalFill) goalFill.style.width = `${pct.toFixed(1)}%`;
    if (streakEl) streakEl.textContent = t('goal.streak', { days: n(snapshot.streak_days || 0) });

    const brkCard = $('tc-break-card'), brkText = $('tc-break-text'), brkHint = $('tc-break-hint');
    if (brkCard && brkText) {
      const elapsedMin = Math.floor((snapshot.break_elapsed_sec || 0) / 60);
      const due = !!snapshot.break_due;
      brkCard.classList.toggle('due', due);
      brkText.textContent = due ? t('break.due_title') : t('break.active', { minutes: n(elapsedMin) });
      if (brkHint) brkHint.textContent = due ? t('break.due_hint') : `${d(snapshot.live_cps || 0, 1)} CPS`;
    }

    if (focusActive) {
      const now = Date.now();
      const rem = Math.max(0, Math.ceil((focusEndMs - now) / 1000));
      const totalNow = (snapshot.lifetime?.keyboard || 0) + (snapshot.lifetime?.mouse || 0);
      const delta = Math.max(0, totalNow - focusStartCounts);
      const mm = String(Math.floor(rem / 60)).padStart(2, '0');
      const ss = String(rem % 60).padStart(2, '0');
      const clk = $('tc-focus-clock');
      if (clk) clk.textContent = `${mm}:${ss} · ${n(delta)}`;
      if (rem === 0) {
        stopFocus(true, delta, snapshot);
      }
    }
  };

  window.renderTypeCountApps = snapshot => {
    if (!snapshot) return;
    const listEl = $('tc-apps-list'), emptyEl = $('tc-apps-empty');
    if (!listEl) return;
    const rows = appsPeriod === 'lifetime' ? (snapshot.apps_lifetime || []) : (snapshot.apps_today || []);
    const excluded = new Set(window.presentationView?.excluded_apps || []);
    const visible = rows.filter(r => !excluded.has(r.app));
    if (!visible.length) {
      listEl.replaceChildren();
      if (emptyEl) emptyEl.hidden = false;
      return;
    }
    if (emptyEl) emptyEl.hidden = true;
    const grandTotal = visible.reduce((s, r) => s + (r.keyboard || 0) + (r.mouse || 0), 0) || 1;
    const maxRow = Math.max(1, ...visible.map(r => (r.keyboard || 0) + (r.mouse || 0)));

    const cards = visible.map(r => {
      const total = (r.keyboard || 0) + (r.mouse || 0);
      const share = total / grandTotal;
      const barPct = (total / maxRow) * 100;
      const kbPart = total ? (r.keyboard / total) * barPct : 0;
      const msPart = total ? (r.mouse / total) * barPct : 0;

      const row = document.createElement('div');
      row.className = 'tc-app-row';

      const nameCol = document.createElement('div');
      nameCol.className = 'tc-app-name';
      const badge = document.createElement('span');
      badge.className = 'tc-app-badge';
      badge.textContent = (r.app || '?').replace(/\.exe$/i, '').slice(0, 2);
      const title = document.createElement('span');
      title.className = 'tc-app-title';
      title.textContent = r.app;
      title.title = r.app;
      nameCol.append(badge, title);

      const barWrap = document.createElement('div');
      barWrap.className = 'tc-app-bar-wrap';
      const track = document.createElement('div');
      track.className = 'tc-app-bar-track';
      const kbFill = document.createElement('div');
      kbFill.className = 'tc-app-bar-kb';
      kbFill.style.width = `${kbPart.toFixed(1)}%`;
      const msFill = document.createElement('div');
      msFill.className = 'tc-app-bar-ms';
      msFill.style.width = `${msPart.toFixed(1)}%`;
      track.append(kbFill, msFill);
      const sub = document.createElement('div');
      sub.className = 'tc-app-nums';
      sub.textContent = `${t('common.keyboard')} ${n(r.keyboard)} · ${t('common.mouse')} ${n(r.mouse)} · ${t('apps.col_scroll')} ${n(r.scroll || 0)}`;
      barWrap.append(track, sub);

      const timeEl = document.createElement('div');
      timeEl.className = 'tc-app-nums';
      timeEl.textContent = fmtDuration(r.active_sec || 0);

      const shareEl = document.createElement('div');
      shareEl.className = 'tc-app-nums';
      shareEl.textContent = window.ClickPulseI18n ? window.ClickPulseI18n.percent(share, 1) : '';

      const totEl = document.createElement('div');
      totEl.className = 'tc-app-total';
      totEl.textContent = n(total);

      row.append(nameCol, barWrap, timeEl, shareEl, totEl);
      return row;
    });
    listEl.replaceChildren(...cards);
  };

  window.renderTypeCountSettings = () => {
    const v = window.presentationView || {};
    const setVal = (id, val) => { const el = $(id); if (el && val !== undefined) el.value = String(val); };
    const setChk = (id, val) => { const el = $(id); if (el) el.checked = !!val; };
    setVal('tc-startup-mode-select', v.startup_mode || 'tray');
    setVal('tc-wake-hotkey-select', v.wake_hotkey || 'Ctrl+Alt+Shift+T');
    setVal('tc-daily-goal-input', v.daily_goal || 8000);
    setVal('tc-break-interval-select', String(v.break_interval_min ?? 45));
    setVal('tc-dpi-input', v.dpi_ppi || 96);
    setChk('tc-track-apps-switch', v.track_apps !== false);
    setChk('tc-auto-backup-switch', v.auto_backup !== false);
    if (window.ClickPulseDropdowns?.sync) {
      for (const id of ['tc-startup-mode-select', 'tc-wake-hotkey-select', 'tc-break-interval-select']) {
        window.ClickPulseDropdowns.sync(id);
      }
    }
  };

  function startFocus() {
    focusActive = true;
    focusEndMs = Date.now() + 25 * 60 * 1000;
    const snap = window._lastSnapshot;
    focusStartCounts = (snap?.lifetime?.keyboard || 0) + (snap?.lifetime?.mouse || 0);
    const btn = $('tc-focus-btn');
    if (btn) btn.textContent = t('focus.stop');
    clearInterval(focusTimerHandle);
    focusTimerHandle = setInterval(() => {
      if (!focusActive) return;
      const rem = Math.max(0, Math.ceil((focusEndMs - Date.now()) / 1000));
      const mm = String(Math.floor(rem / 60)).padStart(2, '0');
      const ss = String(rem % 60).padStart(2, '0');
      const clk = $('tc-focus-clock');
      if (clk) clk.textContent = `${mm}:${ss}`;
      if (rem === 0) stopFocus(true, 0, window._lastSnapshot);
    }, 1000);
  }

  function stopFocus(completed, countDelta = 0, snap = null) {
    focusActive = false;
    clearInterval(focusTimerHandle);
    focusTimerHandle = null;
    const btn = $('tc-focus-btn');
    if (btn) btn.textContent = t('focus.start');
    const clk = $('tc-focus-clock');
    if (clk) clk.textContent = '25:00';
    if (completed) {
      const dur = 25 * 60;
      const totalNow = (snap?.lifetime?.keyboard || 0) + (snap?.lifetime?.mouse || 0);
      const delta = countDelta || Math.max(0, totalNow - focusStartCounts);
      send('save_focus_session', { duration_sec: dur, total: delta });
      window.toast?.(t('focus.completed'));
    }
  }

  function wire() {
    $('tc-apps-today-btn')?.addEventListener('click', () => {
      appsPeriod = 'today';
      $('tc-apps-today-btn')?.classList.add('selected');
      $('tc-apps-life-btn')?.classList.remove('selected');
      if (window._lastSnapshot) window.renderTypeCountApps(window._lastSnapshot);
    });
    $('tc-apps-life-btn')?.addEventListener('click', () => {
      appsPeriod = 'lifetime';
      $('tc-apps-life-btn')?.classList.add('selected');
      $('tc-apps-today-btn')?.classList.remove('selected');
      if (window._lastSnapshot) window.renderTypeCountApps(window._lastSnapshot);
    });
    $('tc-break-reset')?.addEventListener('click', () => send('reset_break'));
    $('tc-focus-btn')?.addEventListener('click', () => {
      if (focusActive) stopFocus(false);
      else startFocus();
    });
    $('tc-stealth-enter')?.addEventListener('click', () => send('enter_stealth'));
    $('tc-stealth-header-btn')?.addEventListener('click', () => send('enter_stealth'));
    $('tc-startup-mode-select')?.addEventListener('change', e => send('startup_mode', { mode: e.target.value }));
    $('tc-wake-hotkey-select')?.addEventListener('change', e => send('wake_hotkey', { hotkey: e.target.value }));
    $('tc-daily-goal-input')?.addEventListener('change', e => send('daily_goal', { goal: Number(e.target.value) || 8000 }));
    $('tc-break-interval-select')?.addEventListener('change', e => send('break_interval', { minutes: Number(e.target.value) || 0 }));
    $('tc-dpi-input')?.addEventListener('change', e => send('dpi_ppi', { ppi: Number(e.target.value) || 96 }));
    $('tc-track-apps-switch')?.addEventListener('change', e => send('track_apps', { enabled: e.target.checked }));
    $('tc-auto-backup-switch')?.addEventListener('change', e => send('auto_backup', { enabled: e.target.checked }));
    $('tc-export-csv')?.addEventListener('click', () => send('export_csv'));
    $('tc-export-json')?.addEventListener('click', () => send('export_json'));
    $('tc-backup-now')?.addEventListener('click', () => send('backup_now'));
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', wire);
  else wire();
})();
