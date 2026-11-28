/* TypeCount landing — page interactions + live dashboard bound to real input.
 * The counting engine lives in live.js (window.TypeCountLive). Nothing here
 * invents numbers: every value comes from actual activity on this page.
 */
(function () {
  "use strict";
  const $ = (s, r) => (r || document).querySelector(s);
  const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
  const fmt = (n) => Math.round(n || 0).toLocaleString("en-US");
  const Live = window.TypeCountLive;

  /* ---------- toast ---------- */
  const toast = $("#toast");
  let toastTimer;
  function showToast(msg) {
    if (!toast) return;
    toast.textContent = msg;
    toast.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove("show"), 2800);
  }

  /* ---------- mobile nav ---------- */
  const burger = $("#burger");
  burger.addEventListener("click", () => {
    const open = $(".nav-links").classList.toggle("open");
    burger.setAttribute("aria-expanded", String(open));
  });
  $$(".nav-links a").forEach((a) => a.addEventListener("click", () => $(".nav-links").classList.remove("open")));

  /* ---------- reveal on scroll ---------- */
  const io = new IntersectionObserver(
    (entries) => entries.forEach((e) => { if (e.isIntersecting) { e.target.classList.add("in"); io.unobserve(e.target); } }),
    { threshold: 0.12 }
  );
  $$(".reveal").forEach((el) => io.observe(el));

  /* ============================================================
     LIVE DASHBOARD
     ============================================================ */
  const el = {
    mToday: $("#mToday"), mWeek: $("#mWeek"), mLife: $("#mLife"),
    mTodayDetail: $("#mTodayDetail"), mWeekDetail: $("#mWeekDetail"), mLifeDetail: $("#mLifeDetail"),
    wpm: $("#liveWpm"), cpm: $("#liveCpm"), scroll: $("#liveScroll"), peak: $("#peakWpm"),
    move: $("#liveMove"), session: $("#liveSession"),
    odoNum: $("#odoNum"), odoBar: $("#odoBar"), odoNote: $("#odoNote"),
    strainKeys: $("#strainKeys"), strainBadge: $("#strainBadge"), strainBar: $("#strainBar"), strainNote: $("#strainNote"),
    chart: $("#hourlyChart"), donut: $("#donut"), donutLegend: $("#donutLegend"),
    heatmap: $("#heatmap"), calTotal: $("#calTotal"),
    streak: $("#streakValue"), status: $("#captureStatus"),
  };

  let lastSnap = null;
  let revealed = false;

  function animateNumber(node, to) {
    if (!node) return;
    const from = parseFloat(node.dataset.val || "0");
    if (!revealed) { node.textContent = fmt(to); node.dataset.val = to; return; }
    const t0 = performance.now(), dur = 500;
    function step(t) {
      const p = Math.min(1, (t - t0) / dur);
      const v = from + (to - from) * (1 - Math.pow(1 - p, 3));
      node.textContent = fmt(v);
      if (p < 1) requestAnimationFrame(step);
      else node.dataset.val = to;
    }
    requestAnimationFrame(step);
  }

  function renderMetrics(s) {
    animateNumber(el.mToday, s.today.k);
    animateNumber(el.mWeek, s.week.k);
    animateNumber(el.mLife, s.all.k);
    if (el.mTodayDetail) el.mTodayDetail.textContent = fmt(s.today.c) + " clicks · " + fmt(s.today.s) + " scrolls · " + s.today.m.toFixed(1) + " m moved";
    if (el.mWeekDetail) el.mWeekDetail.textContent = fmt(s.week.c) + " clicks · avg " + fmt(s.week.k / 7) + " keys/day";
    if (el.mLifeDetail) el.mLifeDetail.textContent = fmt(s.all.c) + " clicks · " + fmt(s.all.days) + " active day" + (s.all.days === 1 ? "" : "s");
  }

  function renderLive(s) {
    const wpm = s.wpm;
    if (el.wpm) el.wpm.textContent = wpm.toFixed(1);
    if (el.cpm) el.cpm.textContent = fmt(s.cpm) + " CPM";
    if (el.scroll) el.scroll.textContent = fmt(s.session.s) + " scrolls this session";
    if (el.peak) el.peak.textContent = "Peak today: " + fmt(s.peakWpm) + " WPM";
    if (el.move) el.move.textContent = s.session.m.toFixed(1) + " m cursor travel";
    if (el.session) {
      const secs = s.session.seconds;
      const mm = Math.floor(secs / 60), ss = secs % 60;
      el.session.textContent = "Session " + mm + ":" + String(ss).padStart(2, "0") + " · " + fmt(s.session.k) + " keys · " + fmt(s.session.c) + " clicks";
    }
    const liveWrap = $("#liveStrip");
    if (liveWrap) liveWrap.classList.toggle("is-idle", s.idle);
    const state = $("#liveState");
    if (state) {
      state.textContent = !s.idle
        ? "typing — " + fmt(s.session.k) + " keys this session"
        : s.session.k || s.session.c || s.session.s
          ? "idle · session total " + fmt(s.session.k) + " keys"
          : "waiting for input…";
    }

    /* mouse odometer — real cursor distance, 500 m daily goal */
    if (el.odoNum) el.odoNum.textContent = s.today.m < 1000 ? s.today.m.toFixed(1) + " m" : fmt(s.today.m) + " m";
    if (el.odoBar) el.odoBar.style.width = Math.min(100, (s.today.m / 500) * 100) + "%";
    if (el.odoNote) el.odoNote.textContent = "Goal: 500 m/day · streak: " + fmt(s.streak) + " day" + (s.streak === 1 ? "" : "s") + (s.streak > 0 ? " 🔥" : "");

    /* strain guard from the real last-hour count */
    if (el.strainKeys) el.strainKeys.textContent = fmt(s.lastHourKeys);
    const lvl = s.lastHourKeys;
    const strain = lvl > 6000 ? "high" : lvl > 3000 ? "moderate" : "low";
    if (el.strainBadge) {
      el.strainBadge.textContent = strain === "low" ? "Healthy" : strain === "moderate" ? "Watch it" : "Take a break";
      el.strainBadge.className = "badge " + (strain === "low" ? "ok" : strain === "moderate" ? "warn" : "err");
    }
    if (el.strainBar) {
      el.strainBar.className = "bar-fill" + (strain === "low" ? "" : " warn");
      el.strainBar.style.width = Math.min(100, (lvl / 8000) * 100) + "%";
    }
    if (el.strainNote) el.strainNote.textContent = "Strain level: " + strain + " · " + (lvl === 0 ? "nothing logged this hour yet" : fmt(lvl) + " keys in the last hour");
  }

  /* ---------- hourly / daily chart ---------- */
  const SVG_NS = "http://www.w3.org/2000/svg";
  let chartRange = "24h";

  function chartSeries(s) {
    if (chartRange === "24h") {
      return s.hours.map((h) => ({ label: String(h.hour).padStart(2, "0") + ":00", k: h.k, c: h.c }));
    }
    const n = chartRange === "7d" ? 7 : 30;
    const src = chartRange === "7d" ? s.series7 : s.series30;
    return src.slice(-n).map((d) => ({
      label: d.date.toLocaleDateString("en-US", { month: "short", day: "numeric" }),
      k: d.k, c: d.c,
    }));
  }

  function drawChart(s) {
    if (!el.chart) return;
    const data = chartSeries(s);
    const max = Math.max(10, ...data.map((d) => Math.max(d.k, d.c)));
    const W = 560, H = 180, pad = 6, gap = 2;
    const bw = (W - pad * 2) / data.length;

    const tip = document.createElement("div");
    tip.className = "chart-tip";
    el.chart.innerHTML = "";
    el.chart.appendChild(tip);

    const svg = document.createElementNS(SVG_NS, "svg");
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);
    svg.setAttribute("preserveAspectRatio", "none");

    /* baseline */
    const axis = document.createElementNS(SVG_NS, "line");
    axis.setAttribute("x1", 0); axis.setAttribute("x2", W);
    axis.setAttribute("y1", H - 18); axis.setAttribute("y2", H - 18);
    axis.setAttribute("stroke", "#23324d"); axis.setAttribute("stroke-width", 1);
    svg.appendChild(axis);

    const empty = data.every((d) => d.k === 0 && d.c === 0);

    data.forEach((d, i) => {
      const x = pad + i * bw;
      const w = Math.max(2, bw - gap);
      const hK = (d.k / max) * (H - 30);
      const hC = (d.c / max) * (H - 30);
      if (d.k > 0) {
        const r = document.createElementNS(SVG_NS, "rect");
        r.setAttribute("x", x); r.setAttribute("y", H - 18 - hK);
        r.setAttribute("width", w); r.setAttribute("height", Math.max(2, hK));
        r.setAttribute("rx", 2); r.setAttribute("fill", "#4f86f7");
        r.setAttribute("class", "bar");
        svg.appendChild(r);
      }
      if (d.c > 0) {
        const r2 = document.createElementNS(SVG_NS, "rect");
        r2.setAttribute("x", x + w / 2 - Math.max(1, w / 4));
        r2.setAttribute("y", H - 18 - hC);
        r2.setAttribute("width", Math.max(1, w / 2));
        r2.setAttribute("height", Math.max(1, hC));
        r2.setAttribute("rx", 1); r2.setAttribute("fill", "#56c2f5");
        r2.setAttribute("opacity", ".75");
        r2.setAttribute("class", "bar");
        svg.appendChild(r2);
      }
      /* invisible hit area so hover works on empty bars too */
      const hit = document.createElementNS(SVG_NS, "rect");
      hit.setAttribute("x", x); hit.setAttribute("y", 0);
      hit.setAttribute("width", Math.max(2, bw)); hit.setAttribute("height", H);
      hit.setAttribute("fill", "transparent");
      hit.addEventListener("mouseenter", (ev) => {
        tip.textContent = d.label + " · " + fmt(d.k) + " keys · " + fmt(d.c) + " clicks";
        tip.style.opacity = "1";
        const box = el.chart.getBoundingClientRect();
        tip.style.left = Math.max(0, Math.min(box.width - 150, ev.clientX - box.left + 10)) + "px";
        tip.style.top = Math.max(0, ev.clientY - box.top - 36) + "px";
      });
      hit.addEventListener("mouseleave", () => (tip.style.opacity = "0"));
      svg.appendChild(hit);
    });

    el.chart.appendChild(svg);

    const emptyMsg = $("#chartEmpty");
    if (emptyMsg) emptyMsg.style.display = empty ? "flex" : "none";
    const chartCaption = $("#chartCaption");
    if (chartCaption) {
      chartCaption.textContent = chartRange === "24h"
        ? "Today, " + new Date().toLocaleDateString("en-US", { month: "short", day: "numeric" })
        : chartRange === "7d" ? "Last 7 days" : "Last 30 days";
    }
  }

  /* ---------- donut: real activity mix ---------- */
  function drawDonut(s) {
    if (!el.donut) return;
    const k = s.today.k, c = s.today.c, sc = s.today.s;
    const total = k + c + sc;
    const parts = total === 0
      ? [{ label: "Keyboard", value: 0, color: "#4f86f7" },
         { label: "Clicks", value: 0, color: "#56c2f5" },
         { label: "Scrolls", value: 0, color: "#7cb0ff" }]
      : [
          { label: "Keyboard", value: Math.round((k / total) * 100), color: "#4f86f7" },
          { label: "Clicks", value: Math.round((c / total) * 100), color: "#56c2f5" },
          { label: "Scrolls", value: 100 - Math.round((k / total) * 100) - Math.round((c / total) * 100), color: "#7cb0ff" },
        ];

    const cx = 75, cy = 75, r = 58, sw = 22;
    const C = 2 * Math.PI * r;
    let offset = 0;
    const svg = document.createElementNS(SVG_NS, "svg");
    svg.setAttribute("viewBox", "0 0 150 150");

    const track = document.createElementNS(SVG_NS, "circle");
    track.setAttribute("cx", cx); track.setAttribute("cy", cy); track.setAttribute("r", r);
    track.setAttribute("fill", "none"); track.setAttribute("stroke", "#1b2842");
    track.setAttribute("stroke-width", sw);
    svg.appendChild(track);

    if (total > 0) {
      parts.forEach((p) => {
        if (p.value <= 0) return;
        const len = (p.value / 100) * C;
        const arc = document.createElementNS(SVG_NS, "circle");
        arc.setAttribute("cx", cx); arc.setAttribute("cy", cy); arc.setAttribute("r", r);
        arc.setAttribute("fill", "none"); arc.setAttribute("stroke", p.color);
        arc.setAttribute("stroke-width", sw);
        arc.setAttribute("stroke-dasharray", (len - 3) + " " + (C - len + 3));
        arc.setAttribute("stroke-dashoffset", -offset);
        arc.setAttribute("transform", "rotate(-90 " + cx + " " + cy + ")");
        svg.appendChild(arc);
        offset += len;
      });
    }

    const t1 = document.createElementNS(SVG_NS, "text");
    t1.setAttribute("x", cx); t1.setAttribute("y", cy + 2);
    t1.setAttribute("text-anchor", "middle"); t1.setAttribute("fill", "#e6eefc");
    t1.setAttribute("font-size", total ? "22" : "13"); t1.setAttribute("font-weight", "800");
    t1.textContent = total ? fmt(total) : "No input";
    svg.appendChild(t1);
    const t2 = document.createElementNS(SVG_NS, "text");
    t2.setAttribute("x", cx); t2.setAttribute("y", cy + 20);
    t2.setAttribute("text-anchor", "middle"); t2.setAttribute("fill", "#90a4c2");
    t2.setAttribute("font-size", "9");
    t2.textContent = total ? "events today" : "yet today";
    svg.appendChild(t2);

    el.donut.innerHTML = "";
    el.donut.appendChild(svg);

    el.donutLegend.innerHTML = parts.map((p) =>
      '<div><strong>' + p.value + '%</strong><span><i class="dot" style="background:' + p.color + '"></i>' + p.label + "</span></div>"
    ).join("");
  }

  /* ---------- 365-day calendar from real days ---------- */
  function levelOf(k) {
    if (!k) return 0;
    if (k < 200) return 1;
    if (k < 800) return 2;
    if (k < 2500) return 3;
    return 4;
  }
  function drawCalendar(s) {
    if (!el.heatmap) return;
    const cells = s.calendar;
    const frag = document.createDocumentFragment();
    let total = 0;
    cells.forEach((d) => {
      total += d.k;
      const cell = document.createElement("div");
      cell.className = "heat-cell level-" + levelOf(d.k);
      cell.title = d.date.toDateString() + " · " + fmt(d.k) + " keys · " + fmt(d.c) + " clicks";
      frag.appendChild(cell);
    });
    el.heatmap.innerHTML = "";
    el.heatmap.appendChild(frag);
    if (el.calTotal) el.calTotal.textContent = fmt(total) + " keys in the last year";
    if (el.streak) el.streak.textContent = fmt(s.streak);
  }

  /* ---------- capture status line ---------- */
  function renderStatus(s) {
    if (!el.status) return;
    el.status.classList.toggle("warn", !s.storageOK);
    el.status.textContent = s.storageOK
      ? "● Counting live · saved in this browser only"
      : "● Counting live · this browser blocks storage, so numbers reset on reload";
  }

  function render(s) {
    lastSnap = s;
    renderMetrics(s);
    renderLive(s);
    drawChart(s);
    drawDonut(s);
    drawCalendar(s);
    renderStatus(s);
  }

  /* first paint, then subscribe */
  render(Live.snapshot());
  Live.subscribe((s) => render(s));
  $("#simWindow").addEventListener("pointerdown", () => { revealed = true; }, { once: true });
  setTimeout(() => { revealed = true; }, 800);

  /* range switcher */
  $("#rangeSeg").addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn) return;
    $$("#rangeSeg button").forEach((b) => b.classList.remove("selected"));
    btn.classList.add("selected");
    chartRange = btn.dataset.range;
    drawChart(Live.snapshot());
  });

  /* reset + export */
  const resetBtn = $("#resetLive");
  if (resetBtn) resetBtn.addEventListener("click", () => {
    if (!confirm("Clear all activity counted in this browser? This cannot be undone.")) return;
    Live.reset();
    showToast("Activity cleared — counting starts fresh.");
  });

  const exportBtn = $("#exportLive");
  if (exportBtn) exportBtn.addEventListener("click", () => {
    const blob = new Blob([Live.exportJSON()], { type: "application/json" });
    const a = document.createElement("a");
    a.href = URL.createObjectURL(blob);
    a.download = "typecount-activity-" + new Date().toISOString().slice(0, 10) + ".json";
    a.click();
    URL.revokeObjectURL(a.href);
    showToast("Exported your local activity as JSON.");
  });

  /* sidebar buttons — Overview only in this page */
  $$("[data-nav]").forEach((b) => b.addEventListener("click", () => {
    if (b.dataset.nav === "Overview") return;
    showToast(b.dataset.nav + " lives in the full desktop app — download it below ↓");
  }));

  /* ============================================================
     Latest release — resolve direct download links via GitHub API.
     Fallback: hrefs already point at /releases/latest.
     ============================================================ */
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
    fetch("https://api.github.com/repos/Amirmpm/typeCount/releases/latest", {
      headers: { Accept: "application/vnd.github+json" },
    })
      .then((r) => (r.ok ? r.json() : Promise.reject(new Error("HTTP " + r.status))))
      .then((rel) => {
        const assets = rel.assets || [];
        Object.keys(PATTERNS).forEach((key) => {
          const hit = assets.find((a) => PATTERNS[key].test(a.name));
          if (hit) $$("[data-asset='" + key + "']").forEach((a) => (a.href = hit.browser_download_url));
        });
        const tag = rel.tag_name || "";
        const vt = $("#versionText");
        if (vt && tag) vt.textContent = tag;
        const dv = $("#dlVersion");
        if (dv && tag) dv.textContent = "Latest: " + tag;
        const sv = $("#simVersion");
        if (sv && tag) sv.textContent = tag.replace(/^v/, "");
      })
      .catch(() => { /* offline / rate-limited: /releases/latest fallback stays */ });
  })();
})();
