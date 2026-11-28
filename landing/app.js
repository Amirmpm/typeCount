/* TypeCount landing — UI behaviour + live dashboard rendering.
 * All numbers come from live.js (real measured input). Nothing is invented.
 */
(function () {
  "use strict";

  const $ = (s, r) => (r || document).querySelector(s);
  const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
  const fmt = (n) => Math.round(Number(n) || 0).toLocaleString("en-US");
  const Live = window.TypeCountLive || null;
  if (!Live) {
    if (window.console) console.error("[TypeCount] live.js failed to load — dashboard disabled, page still works.");
  }
  const reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  /* ═════════ toast ═════════ */
  const toastEl = $("#toast");
  let toastTimer;
  function toast(msg) {
    if (!toastEl) return;
    toastEl.textContent = msg;
    toastEl.classList.add("is-shown");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toastEl.classList.remove("is-shown"), 3000);
  }

  /* ═════════ sticky nav + scroll progress + current section ═════════ */
  const nav = $("#nav");
  const progress = $("#scrollProgress");
  const navLinks = $$("[data-navlink]");
  const sections = navLinks
    .map((a) => document.getElementById(a.getAttribute("href").slice(1)))
    .filter(Boolean);

  let rafPending = false;
  function onScroll() {
    if (rafPending) return;
    rafPending = true;
    requestAnimationFrame(() => {
      rafPending = false;
      const y = window.scrollY;
      if (nav) nav.classList.toggle("is-stuck", y > 8);
      if (progress) {
        const max = document.documentElement.scrollHeight - window.innerHeight;
        progress.style.width = (max > 0 ? (y / max) * 100 : 0) + "%";
      }
      // highlight the section in view
      let current = null;
      const mark = y + window.innerHeight * 0.32;
      sections.forEach((sec) => {
        if (sec.offsetTop <= mark) current = sec.id;
      });
      navLinks.forEach((a) =>
        a.classList.toggle("is-current", a.getAttribute("href") === "#" + current)
      );
    });
  }
  window.addEventListener("scroll", onScroll, { passive: true });
  onScroll();

  /* ═════════ mobile menu ═════════ */
  const burger = $("#burger");
  const navLinksBox = $("#navLinks");
  function setMenu(open) {
    if (!burger || !navLinksBox) return;
    navLinksBox.classList.toggle("is-open", open);
    burger.setAttribute("aria-expanded", String(open));
    burger.setAttribute("aria-label", open ? "Close menu" : "Open menu");
  }
  if (burger) {
    burger.addEventListener("click", () =>
      setMenu(burger.getAttribute("aria-expanded") !== "true")
    );
  }
  $$("#navLinks a").forEach((a) => a.addEventListener("click", () => setMenu(false)));
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") setMenu(false);
  });
  document.addEventListener("click", (e) => {
    if (!navLinksBox || !burger) return;
    if (burger.getAttribute("aria-expanded") !== "true") return;
    if (navLinksBox.contains(e.target) || burger.contains(e.target)) return;
    setMenu(false);
  });

  /* ═════════ reveal on scroll ═════════ */
  if (!reduceMotion && "IntersectionObserver" in window) {
    const io = new IntersectionObserver(
      (entries) =>
        entries.forEach((en) => {
          if (!en.isIntersecting) return;
          en.target.classList.add("is-in");
          io.unobserve(en.target);
        }),
      { threshold: 0.12, rootMargin: "0px 0px -8% 0px" }
    );
    $$(".reveal").forEach((el) => io.observe(el));
  } else {
    $$(".reveal").forEach((el) => el.classList.add("is-in"));
  }

  /* ═════════ elements ═════════ */
  const el = {
    mToday: $("#mToday"), mWeek: $("#mWeek"), mLife: $("#mLife"),
    mTodayDetail: $("#mTodayDetail"), mWeekDetail: $("#mWeekDetail"), mLifeDetail: $("#mLifeDetail"),
    wpm: $("#liveWpm"), cpm: $("#liveCpm"), session: $("#liveSession"),
    scroll: $("#liveScroll"), move: $("#liveMove"), peak: $("#peakWpm"), state: $("#liveState"),
    heroWpm: $("#heroWpm"), heroMeter: $("#heroMeter"), heroKeys: $("#heroKeys"),
    heroClicks: $("#heroClicks"), heroMetres: $("#heroMetres"),
    statKeys: $("#statKeys"), statClicks: $("#statClicks"),
    statScrolls: $("#statScrolls"), statMetres: $("#statMetres"),
    odoNum: $("#odoNum"), odoBar: $("#odoBar"), odoNote: $("#odoNote"),
    strainKeys: $("#strainKeys"), strainBadge: $("#strainBadge"),
    strainBar: $("#strainBar"), strainNote: $("#strainNote"), streak: $("#streakValue"),
    chart: $("#hourlyChart"), chartEmpty: $("#chartEmpty"), chartCaption: $("#chartCaption"),
    donut: $("#donut"), donutLegend: $("#donutLegend"),
    heatmap: $("#heatmap"), calTotal: $("#calTotal"),
    status: $("#captureStatus"),
  };

  /* ═════════ smooth number tweening ═════════
     One rAF loop for every animated value. Values are eased toward their
     target with exponential smoothing, so fast typing and wheel bursts look
     fluid instead of jumping. Only visible panels are animated. */
  const tweens = new Map();
  let tweenRaf = 0;

  function tween(node, target, format) {
    if (!node) return;
    const existing = tweens.get(node);
    if (existing) {
      existing.target = target;
      existing.format = format || existing.format;
      return;
    }
    tweens.set(node, {
      target: target,
      value: Number(node.dataset.v || 0),
      format: format || fmt,
      last: -1,
    });
    if (!tweenRaf) tweenRaf = requestAnimationFrame(stepTweens);
  }

  function stepTweens() {
    tweenRaf = 0;
    let active = false;
    tweens.forEach((t, node) => {
      if (!node.isConnected) { tweens.delete(node); return; }
      const ease = reduceMotion ? 1 : 0.18;
      t.value += (t.target - t.value) * ease;
      if (Math.abs(t.target - t.value) < 0.01) t.value = t.target;
      // write only when the rendered string actually changes
      const text = t.format(t.value);
      if (text !== node.textContent) node.textContent = text;
      node.dataset.v = t.value;
      if (t.value !== t.target) active = true;
      else tweens.delete(node);
    });
    if (active) tweenRaf = requestAnimationFrame(stepTweens);
  }

  const int = (n) => fmt(n);
  const dec1 = (n) => n.toFixed(1);
  const dec2 = (n) => n.toFixed(2);

  /* ═════════ render ═════════ */
  function renderKpis(s) {
    tween(el.mToday, s.today.k, int);
    tween(el.mWeek, s.week.k, int);
    tween(el.mLife, s.all.k, int);
    if (el.mTodayDetail) {
      el.mTodayDetail.textContent = s.today.k
        ? `${fmt(s.today.c)} clicks · ${fmt(s.today.s)} scrolls · ${s.today.m.toFixed(1)} m moved`
        : "no activity yet — start typing";
    }
    if (el.mWeekDetail) {
      el.mWeekDetail.textContent = s.week.k
        ? `${fmt(s.week.c)} clicks · avg ${fmt(s.week.k / 7)}/day`
        : "no activity yet";
    }
    if (el.mLifeDetail) {
      el.mLifeDetail.textContent = s.all.days
        ? `${fmt(s.all.c)} clicks · ${fmt(s.all.days)} active day${s.all.days === 1 ? "" : "s"}`
        : "no activity yet";
    }
  }

  function renderHero(s) {
    tween(el.heroWpm, s.wpm, dec1);
    if (el.heroWpm) el.heroWpm.classList.toggle("is-live", !s.idle && s.wpm > 0);
    if (el.heroMeter) el.heroMeter.style.width = Math.min(100, (s.wpm / 90) * 100) + "%";
    tween(el.heroKeys, s.today.k, int);
    tween(el.heroClicks, s.today.c, int);
    tween(el.heroMetres, s.today.m, (n) => n.toFixed(1) + " m");
  }

  function renderLive(s) {
    tween(el.wpm, s.wpm, dec1);
    tween(el.cpm, s.cpm, (n) => fmt(n) + " CPM");
    if (el.peak) el.peak.textContent = "Peak " + fmt(s.peakWpm) + " WPM";
    if (el.scroll) el.scroll.textContent = fmt(s.session.s);
    tween(el.move, s.session.m, (n) => n.toFixed(1) + " m");
    if (el.session) {
      const secs = s.session.seconds, mm = Math.floor(secs / 60), ss = secs % 60;
      el.session.textContent = mm + ":" + String(ss).padStart(2, "0");
    }
    if (el.state) {
      el.state.textContent = !s.idle
        ? "typing — " + fmt(s.session.k) + " keys this session"
        : s.session.k || s.session.c || s.session.s
          ? "idle · " + fmt(s.session.k) + " keys counted"
          : "waiting for input…";
    }

    /* stat strip — all-time, real */
    tween(el.statKeys, s.all.k, int);
    tween(el.statClicks, s.all.c, int);
    tween(el.statScrolls, s.all.s, int);
    tween(el.statMetres, s.all.m, (n) => (n < 100 ? n.toFixed(1) : fmt(n)));

    /* odometer */
    if (el.odoNum) tween(el.odoNum, s.today.m, (n) => (n < 1000 ? n.toFixed(1) : fmt(n)) + " m");
    if (el.odoBar) el.odoBar.style.width = Math.min(100, (s.today.m / 500) * 100) + "%";
    if (el.odoNote) {
      el.odoNote.textContent =
        "Goal: 500 m/day · " + (s.streak ? fmt(s.streak) + " day streak 🔥" : "no streak yet");
    }
    if (el.streak) el.streak.textContent = fmt(s.streak);

    /* strain guard */
    const keys = s.lastHourKeys;
    const level = keys > 6000 ? "err" : keys > 3000 ? "warn" : "ok";
    if (el.strainKeys) el.strainKeys.textContent = fmt(keys);
    if (el.strainBadge) {
      el.strainBadge.textContent = level === "ok" ? "Healthy" : level === "warn" ? "Watch it" : "Take a break";
      el.strainBadge.className = "chip chip-" + level;
    }
    if (el.strainBar) {
      el.strainBar.className = "fill" + (level === "ok" ? "" : " " + level);
      el.strainBar.style.width = Math.min(100, (keys / 8000) * 100) + "%";
    }
    if (el.strainNote) {
      el.strainNote.textContent = keys
        ? fmt(keys) + " keys this hour · level " + (level === "ok" ? "low" : level === "warn" ? "moderate" : "high")
        : "Nothing logged this hour yet — start typing to see Strain Guard react.";
    }
  }

  /* ═════════ chart ═════════ */
  const NS = "http://www.w3.org/2000/svg";
  let range = "24h";

  function series(s) {
    if (range === "24h") {
      return s.hours.map((h) => ({
        label: String(h.hour).padStart(2, "0") + ":00",
        k: h.k, c: h.c,
      }));
    }
    const src = range === "7d" ? s.series7 : s.series30;
    return src.map((d) => ({
      label: d.date.toLocaleDateString("en-US", { month: "short", day: "numeric" }),
      k: d.k, c: d.c,
    }));
  }

  function drawChart(s) {
    if (!el.chart) return;
    const data = series(s);
    const max = Math.max(4, ...data.map((d) => Math.max(d.k, d.c)));
    const W = 560, H = 176, padL = 2, padR = 2, baseLine = H - 16;
    const bw = (W - padL - padR) / data.length;

    el.chart.textContent = "";
    const tip = document.createElement("div");
    tip.className = "chart-tip";
    el.chart.appendChild(tip);

    const svg = document.createElementNS(NS, "svg");
    svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
    svg.setAttribute("preserveAspectRatio", "none");

    // gridlines
    [0.33, 0.66].forEach((f) => {
      const ln = document.createElementNS(NS, "line");
      ln.setAttribute("x1", 0); ln.setAttribute("x2", W);
      ln.setAttribute("y1", baseLine - (baseLine - 8) * f);
      ln.setAttribute("y2", baseLine - (baseLine - 8) * f);
      ln.setAttribute("stroke", "#1a2740");
      ln.setAttribute("stroke-width", "1");
      ln.setAttribute("stroke-dasharray", "3 5");
      svg.appendChild(ln);
    });

    // baseline
    const axis = document.createElementNS(NS, "line");
    axis.setAttribute("x1", 0); axis.setAttribute("x2", W);
    axis.setAttribute("y1", baseLine); axis.setAttribute("y2", baseLine);
    axis.setAttribute("stroke", "#22334e");
    svg.appendChild(axis);

    let empty = true;
    data.forEach((d, i) => {
      const x = padL + i * bw;
      const w = Math.max(2, bw - 2);
      if (d.k > 0) {
        empty = false;
        const hK = Math.max(2, (d.k / max) * (baseLine - 8));
        const r = document.createElementNS(NS, "rect");
        r.setAttribute("x", x); r.setAttribute("y", baseLine - hK);
        r.setAttribute("width", w); r.setAttribute("height", hK);
        r.setAttribute("rx", Math.min(3, w / 2));
        r.setAttribute("fill", "#4f86f7");
        r.setAttribute("class", "bar");
        svg.appendChild(r);
      }
      if (d.c > 0) {
        empty = false;
        const hC = Math.max(2, (d.c / max) * (baseLine - 8));
        const r2 = document.createElementNS(NS, "rect");
        r2.setAttribute("x", x + w * 0.28); r2.setAttribute("y", baseLine - hC);
        r2.setAttribute("width", Math.max(1.5, w * 0.44)); r2.setAttribute("height", hC);
        r2.setAttribute("rx", 1.5);
        r2.setAttribute("fill", "#56c2f5"); r2.setAttribute("opacity", ".8");
        r2.setAttribute("class", "bar");
        svg.appendChild(r2);
      }

      // hover hit area
      const hit = document.createElementNS(NS, "rect");
      hit.setAttribute("x", x); hit.setAttribute("y", 0);
      hit.setAttribute("width", Math.max(2, bw)); hit.setAttribute("height", H);
      hit.setAttribute("fill", "transparent");
      hit.addEventListener("mouseenter", () => {
        tip.textContent = `${d.label} · ${fmt(d.k)} keys · ${fmt(d.c)} clicks`;
        tip.style.opacity = "1";
        const px = Math.min(92, Math.max(0, ((x + w / 2) / W) * 100));
        tip.style.left = px + "%";
        tip.style.top = "2px";
        tip.style.transform = "translateX(-50%)";
      });
      hit.addEventListener("mouseleave", () => { tip.style.opacity = "0"; });
      svg.appendChild(hit);
    });

    el.chart.appendChild(svg);
    if (el.chartEmpty) el.chartEmpty.style.display = empty ? "grid" : "none";

    if (el.chartCaption) {
      el.chartCaption.textContent =
        range === "24h"
          ? "Today, " + new Date().toLocaleDateString("en-US", { month: "short", day: "numeric" })
          : range === "7d" ? "Last 7 days" : "Last 30 days";
    }
  }

  /* ═════════ donut ═════════ */
  function drawDonut(s) {
    if (!el.donut) return;
    const k = s.today.k, c = s.today.c, sc = s.today.s;
    const total = k + c + sc;
    const pct = (v) => (total ? Math.round((v / total) * 100) : 0);
    const kb = pct(k), cl = pct(c);
    const parts = total
      ? [
          { label: "Keys", value: kb, color: "#4f86f7" },
          { label: "Clicks", value: cl, color: "#56c2f5" },
          { label: "Scrolls", value: Math.max(0, 100 - kb - cl), color: "#8ebcff" },
        ]
      : [
          { label: "Keys", value: 0, color: "#4f86f7" },
          { label: "Clicks", value: 0, color: "#56c2f5" },
          { label: "Scrolls", value: 0, color: "#8ebcff" },
        ];

    const cx = 66, cy = 66, r = 50, sw = 18;
    const C = 2 * Math.PI * r;
    const svg = document.createElementNS(NS, "svg");
    svg.setAttribute("viewBox", "0 0 132 132");

    const track = document.createElementNS(NS, "circle");
    track.setAttribute("cx", cx); track.setAttribute("cy", cy); track.setAttribute("r", r);
    track.setAttribute("fill", "none"); track.setAttribute("stroke", "#182740");
    track.setAttribute("stroke-width", sw);
    svg.appendChild(track);

    let offset = 0;
    if (total) {
      parts.forEach((p) => {
        if (p.value <= 0) return;
        const len = (p.value / 100) * C;
        const arc = document.createElementNS(NS, "circle");
        arc.setAttribute("cx", cx); arc.setAttribute("cy", cy); arc.setAttribute("r", r);
        arc.setAttribute("fill", "none");
        arc.setAttribute("stroke", p.color);
        arc.setAttribute("stroke-width", sw);
        arc.setAttribute("stroke-linecap", "butt");
        arc.setAttribute("stroke-dasharray", `${Math.max(0, len - 2)} ${C - len + 2}`);
        arc.setAttribute("stroke-dashoffset", -offset);
        arc.setAttribute("transform", `rotate(-90 ${cx} ${cy})`);
        svg.appendChild(arc);
        offset += len;
      });
    }

    const t1 = document.createElementNS(NS, "text");
    t1.setAttribute("x", cx); t1.setAttribute("y", cy + (total ? 3 : 0));
    t1.setAttribute("text-anchor", "middle");
    t1.setAttribute("fill", "#eaf1ff");
    t1.setAttribute("font-size", total ? "21" : "11");
    t1.setAttribute("font-weight", "700");
    t1.textContent = total ? fmt(total) : "No input";
    svg.appendChild(t1);

    if (total) {
      const t2 = document.createElementNS(NS, "text");
      t2.setAttribute("x", cx); t2.setAttribute("y", cy + 17);
      t2.setAttribute("text-anchor", "middle");
      t2.setAttribute("fill", "#8ba0bf");
      t2.setAttribute("font-size", "8.5");
      t2.textContent = "events today";
      svg.appendChild(t2);
    }

    el.donut.textContent = "";
    el.donut.appendChild(svg);

    if (el.donutLegend) {
      el.donutLegend.innerHTML = parts
        .map(
          (p) =>
            `<div><strong>${p.value}%</strong><span><i class="dot" style="background:${p.color}"></i>${p.label}</span></div>`
        )
        .join("");
    }
  }

  /* ═════════ calendar ═════════ */
  function levelOf(k) {
    if (!k) return 0;
    if (k < 150) return 1;
    if (k < 600) return 2;
    if (k < 2000) return 3;
    return 4;
  }
  function drawCalendar(s) {
    if (!el.heatmap) return;
    const frag = document.createDocumentFragment();
    let total = 0;
    s.calendar.forEach((d) => {
      total += d.k;
      const cell = document.createElement("div");
      cell.className = "heat-cell l" + levelOf(d.k);
      cell.title = `${d.date.toDateString()} · ${fmt(d.k)} keys · ${fmt(d.c)} clicks`;
      frag.appendChild(cell);
    });
    el.heatmap.textContent = "";
    el.heatmap.appendChild(frag);
    if (el.calTotal) el.calTotal.textContent = fmt(total) + " keys this year";
  }

  function renderStatus(s) {
    if (!el.status) return;
    el.status.textContent = s.storageOK ? "saved locally" : "storage blocked";
    el.status.classList.toggle("warn", !s.storageOK);
  }

  /* Heavy render — throttled. Typing fires many events per second; rebuilding
     the SVG chart on each one is what made the panel feel janky. Numbers update
     immediately (tweened), while chart/donut redraw at most ~5x/second. */
  const HEAVY_INTERVAL = 200;
  let heavyTimer = 0;
  let heavyPending = null;
  let lastHeavy = 0;
  let lastChartSig = "";

  function chartSig(s) {
    // hourly buckets for 24h, day buckets otherwise
    if (range === "24h") return s.hours.map((h) => h.k + "/" + h.c).join(",");
    const src = range === "7d" ? s.series7 : s.series30;
    return src.map((d) => d.k + "/" + d.c).join(",");
  }

  function flushHeavy() {
    heavyTimer = 0;
    const s = heavyPending || Live.snapshot();
    lastHeavy = performance.now();

    const cs = chartSig(s);
    if (cs !== lastChartSig) {
      lastChartSig = cs;
      drawChart(s);
      drawDonut(s);
    }
  }

  function scheduleHeavy(s) {
    heavyPending = s;
    if (heavyTimer) return;
    const wait = Math.max(0, HEAVY_INTERVAL - (performance.now() - lastHeavy));
    heavyTimer = setTimeout(flushHeavy, wait);
  }

  function render(s) {
    renderHero(s);
    renderKpis(s);
    renderLive(s);
    renderStatus(s);
    scheduleHeavy(s);
  }

  /* Calendar is expensive (364 nodes) — rebuilt only when a day value changes,
     and never more than once per second. */
  let lastCalSig = "";
  let calTimer = 0;
  function renderCalendar(s) {
    const next = s.calendar.map((d) => d.k).join(",");
    if (next === lastCalSig) return;
    lastCalSig = next;
    if (calTimer) return;
    calTimer = setTimeout(() => {
      calTimer = 0;
      drawCalendar(Live.snapshot());
    }, 1000);
  }

  /* The dashboard must never take the page down with it: if anything throws
     while rendering, log once and keep the rest of the site fully usable. */
  let dashboardBroken = false;
  function safe(fn, arg) {
    if (dashboardBroken) return;
    try { fn(arg); }
    catch (err) {
      dashboardBroken = true;
      if (window.console) console.error("[TypeCount] dashboard render failed:", err);
    }
  }

  if (Live && typeof Live.snapshot === "function") {
    const first = safe(() => Live.snapshot());
    if (first) {
      safe(render, first);
      safe(renderCalendar, first);
    }
    Live.subscribe((s) => { safe(render, s); safe(renderCalendar, s); });
    Live.subscribeLive((s) => {
      if (dashboardBroken) return;
      safe(renderHero, s);
      safe(renderKpis, s);
      safe(renderLive, s);
    });
  }

  /* ═════════ tabs (proper ARIA + keyboard) ═════════ */
  const tabs = $$('.tab[role="tab"]');
  const panels = tabs.map((t) => document.getElementById(t.getAttribute("aria-controls")));

  function selectTab(index, focus) {
    tabs.forEach((t, i) => {
      const on = i === index;
      t.classList.toggle("is-active", on);
      t.setAttribute("aria-selected", String(on));
      t.tabIndex = on ? 0 : -1;
      if (panels[i]) {
        panels[i].classList.toggle("is-active", on);
        panels[i].hidden = !on;
      }
    });
    if (focus) tabs[index].focus();
  }

  tabs.forEach((tab, i) => {
    tab.tabIndex = i === 0 ? 0 : -1;
    tab.addEventListener("click", () => selectTab(i));
    tab.addEventListener("keydown", (e) => {
      const keys = { ArrowRight: 1, ArrowLeft: -1, Home: -Infinity, End: Infinity };
      if (!(e.key in keys)) return;
      e.preventDefault();
      let next = keys[e.key] === Infinity ? tabs.length - 1
               : keys[e.key] === -Infinity ? 0
               : (i + keys[e.key] + tabs.length) % tabs.length;
      selectTab(next, true);
    });
  });

  /* ═════════ chart range ═════════ */
  const rangeSeg = $("#rangeSeg");
  if (rangeSeg) {
    rangeSeg.addEventListener("click", (e) => {
      const btn = e.target.closest("button[data-range]");
      if (!btn) return;
      $$("button", rangeSeg).forEach((b) => {
        b.classList.toggle("is-active", b === btn);
        b.setAttribute("aria-pressed", String(b === btn));
      });
      range = btn.dataset.range;
      lastChartSig = "";
      drawChart(Live.snapshot());
    });
  }

  /* ═════════ reset / export (only if the engine loaded) ═════════ */
  if (!Live) {
    // still wire the rest of the page, just without data features
  }
  const resetBtn = $("#resetLive");
  if (resetBtn) {
    resetBtn.addEventListener("click", () => {
      if (!window.confirm("Clear all activity counted in this browser? This can't be undone.")) return;
      Live.reset();
      toast("Activity cleared — counting starts fresh.");
    });
  }

  const exportBtn = $("#exportLive");
  if (exportBtn) {
    exportBtn.addEventListener("click", () => {
      const blob = new Blob([Live.exportJSON()], { type: "application/json" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = "typecount-activity-" + new Date().toISOString().slice(0, 10) + ".json";
      a.click();
      setTimeout(() => URL.revokeObjectURL(a.href), 1000);
      toast("Exported your local activity as JSON.");
    });
  }

  /* ═════════ OS detection for the download cards ═════════ */
  (function detectOS() {
    const ua = navigator.userAgent;
    const platform = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || "";
    let os = null;
    if (/Windows|Win32|Win64/i.test(ua) || /Win/i.test(platform)) os = "windows";
    else if (/Android/i.test(ua)) os = null;
    else if (/Mac|iPhone|iPad|iPod/i.test(ua) || /Mac/i.test(platform)) os = "macos";
    else if (/Linux|X11|CrOS/i.test(ua)) os = "linux";
    if (!os) return;
    const card = document.querySelector('.dl-card[data-os="' + os + '"]');
    if (card) card.classList.add("is-detected");
  })();

  /* ═════════ latest release: resolve download links ═════════ */
  (function resolveLatestRelease() {
    const PATTERNS = {
      "win-msi": /windows.*setup.*\.msi$/i,
      "win-setup": /windows.*setup.*\.exe$/i,
      "win-portable": /windows.*portable.*\.exe$/i,
      "mac-dmg": /macos.*setup.*\.dmg$/i,
      "mac-zip": /macos.*portable.*\.zip$/i,
      "linux-deb": /linux.*setup.*\.deb$/i,
      "linux-tar": /linux.*portable.*\.tar\.gz$/i,
    };
    // Cached in sessionStorage so repeat views don't re-hit the API.
    const CACHE_KEY = "typecount.release.v1";
    const render = (rel) => {
      const assets = rel.assets || [];
      Object.keys(PATTERNS).forEach((key) => {
        const hit = assets.find((a) => PATTERNS[key].test(a.name));
        if (hit) $$("[data-asset='" + key + "']").forEach((a) => (a.href = hit.browser_download_url));
      });
      const tag = rel.tag_name || "";
      if (!tag) return;
      const vt = $("#versionText");
      if (vt) vt.textContent = tag;
      const dv = $("#dlVersion");
      if (dv) dv.textContent = "Latest: " + tag + " — every button below grabs this build.";
      const sv = $("#simVersion");
      if (sv) sv.textContent = tag.replace(/^v/, "");
    };

    let cached = null;
    try { cached = JSON.parse(sessionStorage.getItem(CACHE_KEY) || "null"); } catch (e) { /* ignore */ }
    if (cached && cached.cachedAt && Date.now() - cached.cachedAt < 6 * 3600e3) { render(cached); return; }

    fetch("https://api.github.com/repos/Amirmpm/typeCount/releases/latest", {
      headers: { Accept: "application/vnd.github+json" },
    })
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error("HTTP " + r.status))))
      .then((rel) => {
        render(rel);
        try {
          sessionStorage.setItem(CACHE_KEY, JSON.stringify({ ...rel, cachedAt: Date.now() }));
        } catch (e) { /* quota / private mode */ }
      })
      .catch(() => { /* offline or rate limited: /releases/latest fallback keeps working */ });
  })();
})();
