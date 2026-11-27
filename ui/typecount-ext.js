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

  // Typing speed test state
  const PASSAGES = {
    en: [
      'Clean software feels effortless because every small interaction respects your focus and flows without friction.',
      'Consistent daily habits compound faster than rare bursts of intensity when building reliable systems and tools.',
      'Offline desktop analytics give you full ownership of your productivity rhythm with zero telemetry leaving your device.',
      'Good ergonomics starts with listening to your hands, taking calm breaks, and keeping a steady, comfortable posture.'
    ],
    fa: [
      'نرم‌افزار خوب زمانی دلپذیر است که ساده، سریع و کاملاً در اختیار کاربر باشد و حریم خصوصی او را محترم بشمارد.',
      'تداوم آرام و روزانه در کارهای کوچک، نتیجه‌ای بسیار ماندگارتر از تلاش‌های کوتاه‌مدت و پراکنده به همراه دارد.',
      'سلامت مچ دست و چشم‌ها با استراحت‌های کوتاه منظم و ریتم متعادل تایپ کردن در طول روز حفظ می‌شود.',
      'ابزارهای آفلاین دسکتاپ به شما کمک می‌کنند بدون نیاز به اینترنت، الگوی تمرکز و سرعت واقعی خود را بشناسید.'
    ],
    code: [
      'const stats = rows.filter(r => r.active_sec > 0).reduce((acc, r) => acc + r.keyboard + r.mouse, 0);',
      'function clamp(value, min, max) { return Math.min(max, Math.max(min, Number(value) || min)); }',
      'SELECT local_day, SUM(keyboard), SUM(mouse) FROM minute_stats GROUP BY local_day ORDER BY local_day DESC;'
    ]
  };
  let ttLang = 'en';
  let ttDurationSec = 30;
  let ttPassage = PASSAGES.en[0];
  let ttPassageIdx = 0;
  let ttRunning = false;
  let ttFinished = false;
  let ttStartMs = 0;
  let ttInterval = null;
  let ttSpanEls = [];

  function fmtDuration(sec) {
    sec = Math.max(0, Math.floor(sec || 0));
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    if (h > 0) return `${h}h ${m}m`;
    if (m > 0) return `${m}m ${s}s`;
    return `${s}s`;
  }

  function fmtHourSlot(hour) {
    if (!Number.isFinite(hour) || hour < 0 || hour > 23) return '—';
    const hh = String(hour).padStart(2, '0');
    const nxt = String((hour + 1) % 24).padStart(2, '0');
    return `${hh}:00 – ${nxt}:00`;
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

    // Productivity & Ergonomic Insights Card
    const ergoCard = $('tc-ergo-card');
    if (ergoCard) {
      ergoCard.hidden = window.presentationView?.ergo_insights_enabled === false;
      const peakHrEl = $('tc-peak-hour'), focusScoreEl = $('tc-focus-score'), activeTimeEl = $('tc-active-time'),
        strainOpsEl = $('tc-strain-ops'), strainBadgeEl = $('tc-strain-badge');
      if (peakHrEl) peakHrEl.textContent = fmtHourSlot(snapshot.peak_hour);
      if (focusScoreEl) focusScoreEl.textContent = `${n(snapshot.focus_score || 0)} / ${n(100)}`;
      if (activeTimeEl) activeTimeEl.textContent = fmtDuration(snapshot.today_active_sec || 0);
      if (strainOpsEl) strainOpsEl.textContent = t('ergo.ops_10m', { count: n(snapshot.ergo_10m_ops || 0) });
      if (strainBadgeEl) {
        const lvl = Math.max(0, Math.min(2, Number(snapshot.ergo_strain_level || 0)));
        strainBadgeEl.className = `tc-strain-badge level-${lvl}`;
        const key = lvl === 2 ? 'ergo.strain_high' : lvl === 1 ? 'ergo.strain_moderate' : 'ergo.strain_calm';
        strainBadgeEl.textContent = t(key);
      }
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
      if (rem === 0) stopFocus(true, delta, snapshot);
    }
  };

  window.renderTypeCountApps = snapshot => {
    if (!snapshot) return;
    const listEl = $('tc-apps-list'), emptyEl = $('tc-apps-empty');
    if (!listEl) return;
    const rows = appsPeriod === 'lifetime' ? (snapshot.apps_lifetime || []) : (snapshot.apps_today || []);
    const exclEnabled = window.presentationView?.app_exclusions_enabled !== false;
    const excludedArr = Array.isArray(window.presentationView?.excluded_apps) ? window.presentationView.excluded_apps : [];
    const excludedSet = new Set(excludedArr.map(x => String(x).toLowerCase()));
    const visible = exclEnabled ? rows.filter(r => !excludedSet.has(String(r.app || '').toLowerCase())) : rows;

    if (!visible.length) {
      listEl.replaceChildren();
      if (emptyEl) emptyEl.hidden = false;
    } else {
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

        const exBtn = document.createElement('button');
        exBtn.type = 'button';
        exBtn.className = 'tc-app-exclude-btn';
        exBtn.textContent = t('apps.exclude');
        exBtn.addEventListener('click', () => send('exclude_app', { app: r.app }));

        row.append(nameCol, barWrap, timeEl, shareEl, totEl, exBtn);
        return row;
      });
      listEl.replaceChildren(...cards);
    }

    // Privacy Exclusions chips card
    const exclListEl = $('tc-excluded-list'), exclEmptyEl = $('tc-excluded-empty');
    if (exclListEl) {
      exclListEl.replaceChildren(...excludedArr.map(appName => {
        const chip = document.createElement('span');
        chip.className = 'tc-excluded-chip';
        const lbl = document.createElement('span');
        lbl.textContent = appName;
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.textContent = t('apps.restore');
        btn.addEventListener('click', () => send('unexclude_app', { app: appName }));
        chip.append(lbl, btn);
        return chip;
      }));
      if (exclEmptyEl) exclEmptyEl.hidden = excludedArr.length > 0;
    }
  };

  // ---------- Typing Speed Test ----------

  function buildTypingPassage(resetIndex = true) {
    const pool = PASSAGES[ttLang] || PASSAGES.en;
    if (resetIndex) ttPassageIdx = (ttPassageIdx + 1) % pool.length;
    ttPassage = pool[ttPassageIdx];
    const el = $('tc-tt-passage');
    if (el) {
      el.dir = ttLang === 'fa' ? 'rtl' : 'ltr';
      ttSpanEls = [...ttPassage].map((ch, idx) => {
        const sp = document.createElement('span');
        sp.className = 'tc-tt-char' + (idx === 0 ? ' cur' : '');
        sp.textContent = ch;
        return sp;
      });
      el.replaceChildren(...ttSpanEls);
    }
    const inp = $('tc-tt-input');
    if (inp) { inp.value = ''; inp.disabled = false; inp.dir = ttLang === 'fa' ? 'rtl' : 'ltr'; }
    clearInterval(ttInterval);
    ttInterval = null;
    if (ttRunning) send('typing_test_state', { running: false });
    ttRunning = false;
    ttFinished = false;
    ttStartMs = 0;
    if ($('tc-tt-wpm')) $('tc-tt-wpm').textContent = `0 ${t('speed.wpm_unit')}`;
    if ($('tc-tt-acc')) $('tc-tt-acc').textContent = '100%';
    if ($('tc-tt-err')) $('tc-tt-err').textContent = '0';
    if ($('tc-tt-timer-label')) $('tc-tt-timer-label').textContent = `00:${String(ttDurationSec).padStart(2, '0')}`;
  }

  function updateTypingMetrics(typed, forceFinish = false) {
    let correct = 0, errors = 0;
    for (let i = 0; i < ttSpanEls.length; i++) {
      const sp = ttSpanEls[i];
      if (i < typed.length) {
        if (typed[i] === ttPassage[i]) {
          sp.className = 'tc-tt-char ok';
          correct++;
        } else {
          sp.className = 'tc-tt-char bad';
          errors++;
        }
      } else if (i === typed.length) {
        sp.className = 'tc-tt-char cur';
      } else {
        sp.className = 'tc-tt-char';
      }
    }
    const elapsedSec = ttRunning ? Math.max(1, (Date.now() - ttStartMs) / 1000) : 0;
    const remSec = Math.max(0, Math.ceil(ttDurationSec - elapsedSec));
    const wpm = elapsedSec > 0 ? Math.round((correct / 5) * (60 / elapsedSec)) : 0;
    const acc = typed.length > 0 ? Math.round((correct / typed.length) * 1000) / 10 : 100;

    if ($('tc-tt-wpm')) $('tc-tt-wpm').textContent = `${n(wpm)} ${t('speed.wpm_unit')}`;
    if ($('tc-tt-acc')) $('tc-tt-acc').textContent = `${d(acc, 1)}%`;
    if ($('tc-tt-err')) $('tc-tt-err').textContent = n(errors);
    if ($('tc-tt-timer-label')) {
      const mm = String(Math.floor(remSec / 60)).padStart(2, '0');
      const ss = String(remSec % 60).padStart(2, '0');
      $('tc-tt-timer-label').textContent = `${mm}:${ss}`;
    }

    if ((typed.length >= ttPassage.length || remSec <= 0 || forceFinish) && ttRunning && !ttFinished) {
      ttFinished = true;
      ttRunning = false;
      clearInterval(ttInterval);
      ttInterval = null;
      send('typing_test_state', { running: false });
      const actualDur = Math.max(1, Math.min(ttDurationSec, Math.round((Date.now() - ttStartMs) / 1000)));
      const finalWpm = Math.round((correct / 5) * (60 / actualDur) * 10) / 10;
      const rawCpm = Math.round((typed.length * (60 / actualDur)) * 10) / 10;
      if ($('tc-tt-input')) $('tc-tt-input').disabled = true;
      send('save_typing_test', {
        duration_sec: actualDur,
        wpm: finalWpm,
        raw_cpm: rawCpm,
        accuracy: acc,
        errors,
        chars: typed.length,
        lang: ttLang
      });
      window.toast?.(t('typingtest.completed', { wpm: d(finalWpm, 1), acc: d(acc, 1) }));
    }
  }

  window.stopTypeCountTypingTest = () => {
    if (ttRunning) {
      ttRunning = false;
      clearInterval(ttInterval);
      ttInterval = null;
      send('typing_test_state', { running: false });
    }
  };

  window.renderTypeCountTypingTest = snapshot => {
    const bestEl = $('tc-tt-best');
    if (bestEl) {
      const b = Number(snapshot?.typing_test_best_wpm || 0);
      bestEl.textContent = b > 0 ? `${d(b, 1)} WPM` : '—';
    }
    const tbody = $('tc-tt-history'), empty = $('tc-tt-empty');
    if (!tbody) return;
    const rows = snapshot?.typing_tests || snapshot?.typing_tests_recent || [];
    if (!rows.length) {
      tbody.replaceChildren();
      if (empty) empty.hidden = false;
      return;
    }
    if (empty) empty.hidden = true;
    tbody.replaceChildren(...rows.map(r => {
      const tr = document.createElement('tr');
      const dt = window.ClickPulseI18n
        ? window.ClickPulseI18n.date((r.ts || 0) * 1000, { month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit', hourCycle: 'h23' })
        : '';
      for (const val of [dt, `${d(r.wpm, 1)} WPM`, `${d(r.accuracy, 1)}%`, `${r.lang || 'en'} · ${r.duration_sec}s`]) {
        const td = document.createElement('td');
        td.textContent = val;
        tr.append(td);
      }
      return tr;
    }));
  };

  // ---------- Focus-Safe Settings Synchronization ----------

  window.renderTypeCountSettings = () => {
    const v = window.presentationView || {};
    const setVal = (id, val) => {
      const el = $(id);
      // Never overwrite an input while the user is actively typing inside it!
      if (el && val !== undefined && document.activeElement !== el) el.value = String(val);
    };
    const setChk = (id, val) => { const el = $(id); if (el) el.checked = !!val; };

    setVal('tc-startup-mode-select', v.startup_mode || 'tray');
    setVal('tc-wake-hotkey-select', v.wake_hotkey || 'Ctrl+Alt+Shift+T');
    setVal('tc-daily-goal-input', v.daily_goal || 8000);
    setVal('tc-break-interval-select', String(v.break_interval_min ?? 45));
    setVal('tc-dpi-input', v.dpi_ppi || 96);
    setVal('tc-idle-timeout-select', String(v.idle_timeout_sec ?? 90));

    setChk('sport-enabled', v.sport_enabled === true);
    setChk('tc-jalali-switch', v.jalali_calendar === true);
    setChk('tc-typing-test-switch', v.typing_test_enabled !== false);
    setChk('tc-ergo-insights-switch', v.ergo_insights_enabled !== false);
    setChk('tc-track-apps-switch', v.track_apps !== false);
    setChk('tc-app-exclusions-switch', v.app_exclusions_enabled !== false);
    setChk('tc-pause-excluded-switch', v.pause_in_excluded_apps === true);
    setChk('tc-typing-test-stats-switch', v.typing_test_count_stats === true);
    setChk('tc-idle-detection-switch', v.idle_detection_enabled !== false);
    setChk('tc-ergo-guard-switch', v.ergo_guard_enabled !== false);
    setChk('tc-smart-alerts-switch', v.smart_alerts_enabled !== false);
    setChk('tc-sound-alerts-switch', v.sound_alerts_enabled !== false);
    setChk('tc-auto-backup-switch', v.auto_backup !== false);

    const idleRow = $('tc-idle-timeout-row');
    if (idleRow) idleRow.hidden = (v.idle_detection_enabled === false);

    if (window.ClickPulseDropdowns?.sync) {
      for (const id of ['tc-startup-mode-select', 'tc-wake-hotkey-select', 'tc-break-interval-select', 'tc-idle-timeout-select']) {
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

  function commitNumberInput(el, min, max, def, action, field) {
    const val = Math.max(min, Math.min(max, Math.round(Number(el.value) || def)));
    el.value = String(val);
    send(action, { [field]: val });
    window.toast?.(t('message.saved'));
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

    const submitExclude = () => {
      const inp = $('tc-exclude-input');
      const name = String(inp?.value || '').trim();
      if (!name) return;
      send('exclude_app', { app: name });
      inp.value = '';
    };
    $('tc-exclude-add')?.addEventListener('click', submitExclude);
    $('tc-exclude-input')?.addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); submitExclude(); } });

    $('tc-break-reset')?.addEventListener('click', () => send('reset_break'));
    $('tc-focus-btn')?.addEventListener('click', () => { if (focusActive) stopFocus(false); else startFocus(); });
    $('tc-stealth-enter')?.addEventListener('click', () => send('enter_stealth'));
    $('tc-stealth-header-btn')?.addEventListener('click', () => send('enter_stealth'));

    // Settings controls + Enter-key commit for numeric inputs
    $('tc-startup-mode-select')?.addEventListener('change', e => send('startup_mode', { mode: e.target.value }));
    $('tc-wake-hotkey-select')?.addEventListener('change', e => send('wake_hotkey', { hotkey: e.target.value }));
    $('tc-daily-goal-input')?.addEventListener('change', e => commitNumberInput(e.target, 500, 500000, 8000, 'daily_goal', 'goal'));
    $('tc-daily-goal-input')?.addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); e.target.blur(); } });
    $('tc-break-interval-select')?.addEventListener('change', e => send('break_interval', { minutes: Number(e.target.value) || 0 }));
    $('tc-dpi-input')?.addEventListener('change', e => commitNumberInput(e.target, 72, 320, 96, 'dpi_ppi', 'ppi'));
    $('tc-dpi-input')?.addEventListener('keydown', e => { if (e.key === 'Enter') { e.preventDefault(); e.target.blur(); } });
    $('tc-track-apps-switch')?.addEventListener('change', e => send('track_apps', { enabled: e.target.checked }));
    $('tc-auto-backup-switch')?.addEventListener('change', e => send('auto_backup', { enabled: e.target.checked }));
    $('tc-jalali-switch')?.addEventListener('change', e => send('jalali_calendar', { enabled: e.target.checked }));
    $('tc-typing-test-switch')?.addEventListener('change', e => send('typing_test_enabled', { enabled: e.target.checked }));
    $('tc-typing-test-stats-switch')?.addEventListener('change', e => send('typing_test_count_stats', { enabled: e.target.checked }));
    $('tc-ergo-insights-switch')?.addEventListener('change', e => send('ergo_insights_enabled', { enabled: e.target.checked }));
    $('tc-ergo-guard-switch')?.addEventListener('change', e => send('ergo_guard_enabled', { enabled: e.target.checked }));
    $('tc-smart-alerts-switch')?.addEventListener('change', e => send('smart_alerts_enabled', { enabled: e.target.checked }));
    $('tc-sound-alerts-switch')?.addEventListener('change', e => send('sound_alerts_enabled', { enabled: e.target.checked }));
    $('tc-app-exclusions-switch')?.addEventListener('change', e => send('app_exclusions_enabled', { enabled: e.target.checked }));
    $('tc-pause-excluded-switch')?.addEventListener('change', e => send('pause_in_excluded_apps', { enabled: e.target.checked }));
    $('tc-idle-detection-switch')?.addEventListener('change', e => send('idle_detection_enabled', { enabled: e.target.checked }));
    $('tc-idle-timeout-select')?.addEventListener('change', e => send('idle_timeout_sec', { seconds: Number(e.target.value) || 90 }));

    $('tc-export-csv')?.addEventListener('click', () => send('export_csv'));
    $('tc-export-json')?.addEventListener('click', () => send('export_json'));
    $('tc-backup-now')?.addEventListener('click', () => send('backup_now'));

    // Typing Test wiring
    buildTypingPassage(false);
    $('tc-tt-stage')?.addEventListener('click', () => $('tc-tt-input')?.focus());
    $('tc-tt-restart')?.addEventListener('click', () => { buildTypingPassage(true); $('tc-tt-input')?.focus(); });
    $('tc-tt-lang')?.querySelectorAll('[data-tt-lang]').forEach(btn => {
      btn.addEventListener('click', () => {
        ttLang = btn.dataset.ttLang || 'en';
        $('tc-tt-lang').querySelectorAll('[data-tt-lang]').forEach(b => b.classList.toggle('selected', b === btn));
        ttPassageIdx = 0;
        buildTypingPassage(false);
        $('tc-tt-input')?.focus();
      });
    });
    $('tc-tt-dur')?.querySelectorAll('[data-tt-dur]').forEach(btn => {
      btn.addEventListener('click', () => {
        ttDurationSec = Number(btn.dataset.ttDur) || 30;
        $('tc-tt-dur').querySelectorAll('[data-tt-dur]').forEach(b => b.classList.toggle('selected', b === btn));
        buildTypingPassage(false);
        $('tc-tt-input')?.focus();
      });
    });
    $('tc-tt-input')?.addEventListener('input', e => {
      if (ttFinished) return;
      const typed = e.target.value || '';
      if (!ttRunning && typed.length > 0) {
        ttRunning = true;
        ttStartMs = Date.now();
        send('typing_test_state', { running: true });
        clearInterval(ttInterval);
        ttInterval = setInterval(() => updateTypingMetrics($('tc-tt-input')?.value || ''), 250);
      }
      updateTypingMetrics(typed);
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', wire);
  else wire();

  send('ready');
})();
