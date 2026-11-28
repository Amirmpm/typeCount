/* TypeCount landing — simulator + interactions (vanilla JS, no deps) */
(function () {
  "use strict";
  const $ = (s, r) => (r || document).querySelector(s);
  const $$ = (s, r) => Array.from((r || document).querySelectorAll(s));
  const fmt = (n) => Math.round(n).toLocaleString("en-US");

  /* ---------- toast ---------- */
  const toast = $("#toast");
  let toastTimer;
  function showToast(msg) {
    toast.textContent = msg;
    toast.classList.add("show");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => toast.classList.remove("show"), 2600);
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

  /* ---------- count-up ---------- */
  function countUp(el, target, dur) {
    const t0 = performance.now();
    function tick(t) {
      const p = Math.min(1, (t - t0) / (dur || 1200));
      el.textContent = fmt(target * (1 - Math.pow(1 - p, 3)));
      if (p < 1) requestAnimationFrame(tick);
    }
    requestAnimationFrame(tick);
  }
  const cio = new IntersectionObserver(
    (entries) => entries.forEach((e) => {
      if (!e.isIntersecting) return;
      const el = e.target;
      countUp(el, parseFloat(el.dataset.count || "0"));
      cio.unobserve(el);
    }),
    { threshold: 0.4 }
  );
  $$("[data-count]").forEach((el) => cio.observe(el));

  /* ============================================================
     SIMULATOR
     ============================================================ */
  const sim = {
    today: 12483, week: 81240, life: 1843920,
    keys: 0, clicks: 0, scrolls: 0, peak: 0,
    wpm: 0, odo: 187, demo: false, timer: null,
    typeStart: null, typeChars: 0,
  };

  const el = {
    mToday: $("#mToday"), mWeek: $("#mWeek"), mLife: $("#mLife"),
    mTodayDetail: $("#mTodayDetail"), mWeekDetail: $("#mWeekDetail"), mLifeDetail: $("#mLifeDetail"),
    wpm: $("#liveWpm"), cpm: $("#liveCpm"), scroll: $("#liveScroll"), peak: $("#peakWpm"),
    tryInput: $("#tryInput"), tryKeys: $("#tryKeys"), tryChars: $("#tryChars"), tryClicks: $("#tryClicks"),
    odoNum: $("#odoNum"), odoBar: $("#odoBar"), strainKeys: $("#strainKeys"),
    chart: $("#hourlyChart"), donut: $("#donut"), donutLegend: $("#donutLegend"),
    heatmap: $("#heatmap"), calTotal: $("#calTotal"),
  };

  function renderMetrics() {
    el.mToday.textContent = fmt(sim.today);
    el.mWeek.textContent = fmt(sim.week);
    el.mLife.textContent = fmt(sim.life);
    el.mTodayDetail.textContent = fmt(sim.today) + " keystrokes · " + fmt(sim.today * 0.42) + " clicks";
    el.mWeekDetail.textContent = fmt(sim.week) + " keystrokes · avg " + fmt(sim.week / 7) + "/day";
    el.mLifeDetail.textContent = fmt(sim.life) + " keystrokes · " + fmt(sim.life / 12483) + " days active";
  }
  function renderLive() {
    el.wpm.textContent = sim.wpm.toFixed(1);
    el.cpm.textContent = fmt(sim.wpm * 5) + " CPM";
    el.scroll.textContent = fmt(sim.scrolls) + " scrolls";
    el.peak.textContent = "Peak today: " + fmt(sim.peak) + " WPM";
    el.tryKeys.textContent = fmt(sim.keys);
    el.tryChars.textContent = fmt(sim.typeChars);
    el.tryClicks.textContent = fmt(sim.clicks);
    el.odoNum.textContent = fmt(sim.odo) + " m";
    el.odoBar.style.width = Math.min(100, (sim.odo / 500) * 100) + "%";
    el.strainKeys.textContent = fmt(4210 + sim.keys);
  }

  /* count-up metrics when simulator scrolls into view */
  const simIO = new IntersectionObserver(
    (entries) => entries.forEach((e) => {
      if (!e.isIntersecting) return;
      countUp(el.mToday, sim.today); countUp(el.mWeek, sim.week, 1500); countUp(el.mLife, sim.life, 1800);
      simIO.disconnect();
    }),
    { threshold: 0.2 }
  );
  simIO.observe($("#simWindow"));

  /* sidebar / mini-mode buttons → toast (demo scope: Overview only) */
  $$("[data-nav]").forEach((b) => b.addEventListener("click", () => {
    if (b.dataset.nav === "Overview") return;
    showToast(b.dataset.nav + " is available in the full app — download TypeCount below ↓");
  }));

  /* click counter inside the simulator window */
  $("#simWindow").addEventListener("click", () => { sim.clicks++; renderLive(); });

  /* ---------- live typing → WPM/CPM ---------- */
  el.tryInput.addEventListener("input", () => {
    const now = Date.now();
    if (!sim.typeStart) sim.typeStart = now;
    const text = el.tryInput.value;
    sim.typeChars = text.length;
    sim.keys = text.replace(/\s/g, "").length;
    const mins = Math.max((now - sim.typeStart) / 60000, 0.05);
    sim.wpm = (sim.typeChars / 5) / mins;
    if (sim.wpm > sim.peak) sim.peak = sim.wpm;
    sim.today += 1; sim.week += 1; sim.life += 1;
    renderLive(); renderMetrics();
  });
  el.tryInput.addEventListener("keydown", () => {
    sim.scrolls = Math.max(0, sim.scrolls); /* keep scroll counter stable while typing */
  });

  /* ---------- auto-demo ---------- */
  const demoBtn = $("#demoToggle");
  demoBtn.addEventListener("click", () => {
    sim.demo = !sim.demo;
    demoBtn.setAttribute("aria-pressed", String(sim.demo));
    demoBtn.textContent = sim.demo ? "⏸ Auto-demo on" : "▶ Auto-demo";
    if (sim.demo) {
      showToast("Auto-demo: simulating live typing activity…");
      sim.timer = setInterval(() => {
        sim.wpm = Math.max(18, Math.min(92, sim.wpm + (Math.random() - 0.45) * 14));
        if (sim.wpm > sim.peak) sim.peak = sim.wpm;
        const k = Math.round(4 + Math.random() * 22);
        sim.today += k; sim.week += k; sim.life += k; sim.keys += k;
        sim.scrolls += Math.round(Math.random() * 6);
        sim.odo += Math.random() * 1.4;
        renderLive(); renderMetrics();
        shiftChart();
      }, 900);
    } else {
      clearInterval(sim.timer);
      sim.wpm = 0; renderLive();
    }
  });

  /* ---------- hourly chart (SVG bars) ---------- */
  let chartData = [];
  function genChart(range) {
    const n = range === "24h" ? 24 : range === "7d" ? 7 : 30;
    chartData = Array.from({ length: n }, (_, i) => {
      const dayCurve = Math.sin((i / n) * Math.PI); /* bell shape */
      return Math.round(120 + dayCurve * 900 + Math.random() * 380);
    });
    drawChart(range);
  }
  function drawChart(range) {
    const W = 560, H = 180, pad = 6, gap = 3;
    const max = Math.max(...chartData, 1);
    const bw = (W - pad * 2) / chartData.length;
    const tip = document.createElement("div");
    tip.className = "chart-tip";
    el.chart.innerHTML = "";
    el.chart.appendChild(tip);
    const svgNS = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(svgNS, "svg");
    svg.setAttribute("viewBox", `0 0 ${W} ${H}`);
    svg.setAttribute("preserveAspectRatio", "none");
    chartData.forEach((v, i) => {
      const h = Math.max(3, (v / max) * (H - 26));
      const r = document.createElementNS(svgNS, "rect");
      r.setAttribute("x", pad + i * bw + gap / 2);
      r.setAttribute("y", H - 18 - h);
      r.setAttribute("width", Math.max(2, bw - gap));
      r.setAttribute("height", h);
      r.setAttribute("rx", 2);
      r.setAttribute("fill", i === chartData.length - 1 ? "#7cb0ff" : "#4f86f7");
      r.setAttribute("class", "bar");
      const label = range === "24h" ? String(i).padStart(2, "0") + ":00" : range === "7d" ? "Day " + (i + 1) : "Day " + (i + 1);
      r.addEventListener("mouseenter", (e) => {
        tip.textContent = label + " · " + fmt(v) + " keys";
        tip.style.opacity = "1";
        const box = el.chart.getBoundingClientRect();
        tip.style.left = Math.min(box.width - 130, e.clientX - box.left + 10) + "px";
        tip.style.top = (e.clientY - box.top - 34) + "px";
      });
      r.addEventListener("mouseleave", () => (tip.style.opacity = "0"));
      svg.appendChild(r);
    });
    el.chart.appendChild(svg);
  }
  function shiftChart() {
    if (!chartData.length) return;
    chartData.shift();
    chartData.push(Math.round(120 + Math.random() * 1100));
    const active = $("#rangeSeg .selected");
    drawChart(active ? active.dataset.range : "24h");
  }
  $("#rangeSeg").addEventListener("click", (e) => {
    const btn = e.target.closest("button");
    if (!btn) return;
    $$("#rangeSeg button").forEach((b) => b.classList.remove("selected"));
    btn.classList.add("selected");
    genChart(btn.dataset.range);
  });
  genChart("24h");

  /* ---------- donut (activity mix) ---------- */
  function drawDonut() {
    const parts = [
      { label: "Keyboard", value: 58, color: "#4f86f7" },
      { label: "Mouse", value: 31, color: "#56c2f5" },
      { label: "Scroll", value: 11, color: "#7cb0ff" },
    ];
    const cx = 75, cy = 75, r = 58, sw = 22;
    const C = 2 * Math.PI * r;
    let offset = 0;
    const svgNS = "http://www.w3.org/2000/svg";
    const svg = document.createElementNS(svgNS, "svg");
    svg.setAttribute("viewBox", "0 0 150 150");
    parts.forEach((p) => {
      const len = (p.value / 100) * C;
      const c = document.createElementNS(svgNS, "circle");
      c.setAttribute("cx", cx); c.setAttribute("cy", cy); c.setAttribute("r", r);
      c.setAttribute("fill", "none"); c.setAttribute("stroke", p.color);
      c.setAttribute("stroke-width", sw);
      c.setAttribute("stroke-dasharray", `${len - 2} ${C - len + 2}`);
      c.setAttribute("stroke-dashoffset", -offset);
      c.setAttribute("transform", `rotate(-90 ${cx} ${cy})`);
      svg.appendChild(c);
      offset += len;
    });
    const t = document.createElementNS(svgNS, "text");
    t.setAttribute("x", cx); t.setAttribute("y", cy - 2);
    t.setAttribute("text-anchor", "middle");
    t.setAttribute("fill", "#e6eefc");
    t.setAttribute("font-size", "20");
    t.setAttribute("font-weight", "800");
    t.textContent = "58%";
    svg.appendChild(t);
    const t2 = document.createElementNS(svgNS, "text");
    t2.setAttribute("x", cx); t2.setAttribute("y", cy + 16);
    t2.setAttribute("text-anchor", "middle");
    t2.setAttribute("fill", "#90a4c2");
    t2.setAttribute("font-size", "9");
    t2.textContent = "keyboard";
    svg.appendChild(t2);
    el.donut.innerHTML = "";
    el.donut.appendChild(svg);
    el.donutLegend.innerHTML = parts.map((p) =>
      `<div><strong>${p.value}%</strong><span><i class="dot" style="background:${p.color}"></i>${p.label}</span></div>`
    ).join("");
  }
  drawDonut();

  /* ---------- 365-day calendar ---------- */
  (function buildCalendar() {
    const days = 364;
    let total = 0;
    const frag = document.createDocumentFragment();
    for (let i = 0; i < days; i++) {
      const r = Math.random();
      const level = r < 0.18 ? 0 : r < 0.45 ? 1 : r < 0.68 ? 2 : r < 0.88 ? 3 : 4;
      const keys = level === 0 ? 0 : Math.round(800 + level * 2600 + Math.random() * 1800);
      total += keys;
      const d = new Date();
      d.setDate(d.getDate() - (days - i));
      const cell = document.createElement("div");
      cell.className = "heat-cell level-" + level;
      cell.title = d.toDateString() + " · " + fmt(keys) + " keystrokes";
      frag.appendChild(cell);
    }
    el.heatmap.appendChild(frag);
    el.calTotal.textContent = fmt(total) + " keys this year";
  })();

  renderLive();
  renderMetrics();

  /* ============================================================
     Latest release — resolve direct download links via GitHub API.
     Fallback: buttons already point to /releases/latest (release page).
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
          if (hit) {
            $$("[data-asset='" + key + "']").forEach((a) => (a.href = hit.browser_download_url));
          }
        });
        const tag = rel.tag_name || "";
        const vt = $("#versionText");
        if (vt && tag) vt.textContent = tag;
        const dv = $("#dlVersion");
        if (dv && tag) dv.textContent = "Latest: " + tag;
      })
      .catch(() => {
        /* offline / rate-limited: keep /releases/latest fallback links */
      });
  })();
})();
