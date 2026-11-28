'use strict';
const ClickPulseThemes = (() => {
  const palettes = {
    blue: {
      accent: '#2563eb', hover: '#1d4ed8', strong: '#2459a2', navy: '#102543', selected: '#1c3c68', navHover: '#193456',
      background: '#f3f6fc', empty: '#edf4ff', full: '#1f53a0', mouse: '#67c9f4',
      darkAccent: '#4f86f7', darkHover: '#3b73eb', darkStrong: '#7cb0ff', darkNavy: '#0d182b', darkSelected: '#193055', darkNavHover: '#142644',
      darkBg: '#0b1220', darkSurface: '#131d31', darkSurfaceAlt: '#192640', darkText: '#e6eefc', darkMuted: '#90a4c2', darkLine: '#23324d',
      darkEmpty: '#1b2842', darkFull: '#4f86f7', darkMouse: '#56c2f5'
    },
    teal: {
      accent: '#0f8177', hover: '#096a62', strong: '#17685f', navy: '#102d2e', selected: '#1b5051', navHover: '#173d3e',
      background: '#f2f8f7', empty: '#eaf7f4', full: '#14665f', mouse: '#64c9bc',
      darkAccent: '#25a89b', darkHover: '#1c8f84', darkStrong: '#5ed3c6', darkNavy: '#0a1c1e', darkSelected: '#153a3d', darkNavHover: '#112f32',
      darkBg: '#0a1517', darkSurface: '#112124', darkSurfaceAlt: '#162b2e', darkText: '#e4f3f1', darkMuted: '#89ada9', darkLine: '#1f393c',
      darkEmpty: '#172e31', darkFull: '#25a89b', darkMouse: '#56d1c3'
    },
    violet: {
      accent: '#7c3aed', hover: '#6d28d9', strong: '#6639a2', navy: '#251e3c', selected: '#493568', navHover: '#352849',
      background: '#f6f3fc', empty: '#f3edff', full: '#60319c', mouse: '#b49ae8',
      darkAccent: '#9662fa', darkHover: '#824ae8', darkStrong: '#c0a2ff', darkNavy: '#161126', darkSelected: '#32234f', darkNavHover: '#271c3e',
      darkBg: '#100d1c', darkSurface: '#19142c', darkSurfaceAlt: '#221b3b', darkText: '#eeeafd', darkMuted: '#9f94c2', darkLine: '#2d2449',
      darkEmpty: '#241c3d', darkFull: '#9662fa', darkMouse: '#c1a5f4'
    },
    graphite: {
      accent: '#52667e', hover: '#3b4e66', strong: '#344b66', navy: '#182330', selected: '#34465b', navHover: '#253446',
      background: '#f4f6f8', empty: '#edf1f6', full: '#34485f', mouse: '#95aabd',
      darkAccent: '#7189a6', darkHover: '#5d7491', darkStrong: '#9fb6d1', darkNavy: '#10161f', darkSelected: '#263447', darkNavHover: '#1d2938',
      darkBg: '#0d1117', darkSurface: '#151b24', darkSurfaceAlt: '#1c2430', darkText: '#e6ecf4', darkMuted: '#8d9cb0', darkLine: '#273242',
      darkEmpty: '#1f2836', darkFull: '#7189a6', darkMouse: '#9eb6ce'
    }
  };

  const rgb = hex => [1, 3, 5].map(i => parseInt(hex.slice(i, i + 2), 16));
  const mix = (a, b, amount) => '#' + rgb(a).map((value, i) => Math.round(value + (rgb(b)[i] - value) * amount).toString(16).padStart(2, '0')).join('');

  function resolveDark(colorMode, systemDark) {
    if (colorMode === 'dark') return true;
    if (colorMode === 'light') return false;
    if (typeof systemDark === 'boolean') return systemDark;
    try {
      return typeof window !== 'undefined' && typeof window.matchMedia === 'function' && window.matchMedia('(prefers-color-scheme: dark)').matches;
    } catch {
      return false;
    }
  }

  const get = (name, isDark = false) => {
    const selected = Object.prototype.hasOwnProperty.call(palettes, name) ? name : 'blue';
    const raw = palettes[selected];
    if (!isDark) {
      return { ...raw, name: selected, dark: false };
    }
    return {
      ...raw,
      name: selected,
      dark: true,
      accent: raw.darkAccent,
      hover: raw.darkHover,
      strong: raw.darkStrong,
      navy: raw.darkNavy,
      selected: raw.darkSelected,
      navHover: raw.darkNavHover,
      background: raw.darkBg,
      surface: raw.darkSurface,
      empty: raw.darkEmpty,
      full: raw.darkFull,
      mouse: raw.darkMouse
    };
  };

  function tokens(name, isDark = false) {
    const p = get(name, isDark);
    if (!isDark) {
      const soft = mix(p.accent, '#ffffff', 0.94), muted = mix(p.accent, '#ffffff', 0.68);
      return {
        '--bg': p.background,
        '--surface': '#ffffff',
        '--surface-hover': '#f6f9ff',
        '--text': '#172b4a',
        '--muted': '#63758e',
        '--line': '#e1e8f3',
        '--blue': p.accent,
        '--cyan': p.mouse,
        '--navy': p.navy,
        '--accent-hover': p.hover,
        '--accent-strong': p.strong,
        '--accent-soft': soft,
        '--accent-border': mix(p.accent, '#ffffff', 0.76),
        '--focus': mix(p.accent, '#ffffff', 0.3),
        '--control-bg': '#ffffff',
        '--control-text': mix(p.strong, '#172b4a', 0.28),
        '--control-border': mix(p.accent, '#ffffff', 0.78),
        '--control-hover': mix(p.accent, '#ffffff', 0.95),
        '--control-hover-border': mix(p.accent, '#ffffff', 0.55),
        '--nav-selected': p.selected,
        '--nav-hover': p.navHover,
        '--nav-text': mix(p.navy, '#ffffff', 0.75),
        '--nav-muted': mix(p.navy, '#ffffff', 0.6),
        '--nav-icon': mix(p.accent, '#ffffff', 0.55),
        '--nav-line': mix(p.navy, '#ffffff', 0.14),
        '--featured-start': mix(p.strong, p.navy, 0.4),
        '--featured-end': p.strong,
        '--key-border': p.accent + '26',
        '--key-bottom': p.accent + '38',
        '--heat-0': p.empty,
        '--heat-1': mix(p.full, p.empty, 0.76),
        '--heat-2': mix(p.full, p.empty, 0.56),
        '--heat-3': mix(p.full, p.empty, 0.3),
        '--heat-4': p.full,
        '--segment-bg': '#f1f5fb',
        '--segment-border': '#e4ebf5',
        '--segment-active-bg': '#ffffff',
        '--th-bg': '#f1f5fc',
        '--th-text': '#617b9f',
        '--td-text': '#4d6486',
        '--switch-off': '#c5d0df',
        '--switch-knob': '#ffffff',
        '--toast-bg': '#173b6b',
        '--toast-text': '#ffffff',
        '--error-bg': '#fff1f2',
        '--error-text': '#a43b53',
        '--error-border': '#f3c6d0',
        '--warn-bg': '#fffbeb',
        '--warn-text': '#b45309',
        '--warn-border': '#f59e0b',
        '--ok-bg': '#ecfdf5',
        '--ok-text': '#047857',
        '--ok-border': '#a7f3d0',
        '--mouse-shell': '#f9fbff',
        '--mini-start': mix(p.navy, p.accent, 0.12),
        '--mini-end': p.navy,
        '--mini-edge': mix(p.navy, p.accent, 0.35) + '80',
        '--mini-muted': muted,
        '--mini-hover': mix(p.navy, p.accent, 0.4) + '70',
        '--mini-selected': p.accent + '24',
        '--mini-status-bg': p.accent + '1c',
        '--mini-dot': mix(p.accent, '#ffffff', 0.3),
        '--mini-device-bg': muted + '0c',
        '--mini-device-border': muted + '1c',
        '--mini-device-text': mix(p.accent, '#ffffff', 0.82),
        '--mini-device-icon': mix(p.accent, '#ffffff', 0.6)
      };
    }

    const soft = mix(p.accent, p.darkBg, 0.80);
    const muted = mix(p.accent, '#ffffff', 0.65);
    return {
      '--bg': p.darkBg,
      '--surface': p.darkSurface,
      '--surface-hover': p.darkSurfaceAlt,
      '--text': p.darkText,
      '--muted': p.darkMuted,
      '--line': p.darkLine,
      '--blue': p.accent,
      '--cyan': p.mouse,
      '--navy': p.navy,
      '--accent-hover': p.hover,
      '--accent-strong': p.strong,
      '--accent-soft': soft,
      '--accent-border': mix(p.accent, p.darkBg, 0.56),
      '--focus': mix(p.accent, '#ffffff', 0.32),
      '--control-bg': p.darkSurfaceAlt,
      '--control-text': '#dce7f8',
      '--control-border': '#2e4163',
      '--control-hover': mix(p.accent, p.darkSurface, 0.78),
      '--control-hover-border': mix(p.accent, '#ffffff', 0.25),
      '--nav-selected': p.selected,
      '--nav-hover': p.navHover,
      '--nav-text': '#c5d6ef',
      '--nav-muted': '#8ba2c4',
      '--nav-icon': mix(p.accent, '#ffffff', 0.42),
      '--nav-line': '#1d2d47',
      '--featured-start': mix(p.accent, p.navy, 0.55),
      '--featured-end': mix(p.accent, p.navy, 0.25),
      '--key-border': p.accent + '36',
      '--key-bottom': p.accent + '54',
      '--heat-0': p.empty,
      '--heat-1': mix(p.full, p.empty, 0.72),
      '--heat-2': mix(p.full, p.empty, 0.48),
      '--heat-3': mix(p.full, p.empty, 0.24),
      '--heat-4': p.full,
      '--segment-bg': p.darkBg,
      '--segment-border': p.darkLine,
      '--segment-active-bg': p.darkSurfaceAlt,
      '--th-bg': p.darkSurfaceAlt,
      '--th-text': '#a7bad6',
      '--td-text': '#cad7ea',
      '--switch-off': '#344766',
      '--switch-knob': '#f0f5ff',
      '--toast-bg': '#1e355b',
      '--toast-text': '#eef5ff',
      '--error-bg': '#321620',
      '--error-text': '#fda4af',
      '--error-border': '#662839',
      '--warn-bg': '#2c2210',
      '--warn-text': '#fcd34d',
      '--warn-border': '#785013',
      '--ok-bg': '#102b24',
      '--ok-text': '#6ee7b7',
      '--ok-border': '#1b5746',
      '--mouse-shell': '#1c2a42',
      '--mini-start': mix(p.navy, p.accent, 0.16),
      '--mini-end': p.navy,
      '--mini-edge': mix(p.navy, p.accent, 0.4) + '85',
      '--mini-muted': muted,
      '--mini-hover': mix(p.navy, p.accent, 0.42) + '75',
      '--mini-selected': p.accent + '28',
      '--mini-status-bg': p.accent + '22',
      '--mini-dot': mix(p.accent, '#ffffff', 0.35),
      '--mini-device-bg': muted + '0e',
      '--mini-device-border': muted + '20',
      '--mini-device-text': mix(p.accent, '#ffffff', 0.84),
      '--mini-device-icon': mix(p.accent, '#ffffff', 0.62)
    };
  }

  function apply(root, name, colorMode = 'system', systemDark = false) {
    const dark = resolveDark(colorMode, systemDark);
    const p = get(name, dark);
    root.dataset.theme = p.name;
    root.dataset.colorScheme = dark ? 'dark' : 'light';
    root.dataset.colorMode = colorMode || 'system';
    for (const [key, value] of Object.entries(tokens(p.name, dark))) {
      root.style.setProperty(key, value);
    }
    return p;
  }

  const luminance = color => rgb(color).map(c => { const v = c / 255; return v <= 0.04045 ? v / 12.92 : ((v + 0.055) / 1.055) ** 2.4; }).reduce((n, v, i) => n + v * [0.2126, 0.7152, 0.0722][i], 0);
  const contrast = (a, b) => (Math.max(luminance(a), luminance(b)) + 0.05) / (Math.min(luminance(a), luminance(b)) + 0.05);
  const textFor = color => contrast(color, '#ffffff') >= 4.5 ? '#ffffff' : contrast(color, '#172b4a') >= 4.5 ? '#172b4a' : (luminance(color) > 0.35 ? '#0d1522' : '#f8fafc');

  return { get, tokens, apply, resolveDark, rgb, contrast, textFor, names: Object.keys(palettes) };
})();
if (typeof module !== 'undefined') module.exports = ClickPulseThemes;
