'use strict';
const TypeCountJalali = (() => {
  const FA_MONTHS = [
    'فروردین', 'اردیبهشت', 'خرداد', 'تیر', 'مرداد', 'شهریور',
    'مهر', 'آبان', 'آذر', 'دی', 'بهمن', 'اسفند'
  ];
  const EN_MONTHS = [
    'Farvardin', 'Ordibehesht', 'Khordad', 'Tir', 'Mordad', 'Shahrivar',
    'Mehr', 'Aban', 'Azar', 'Dey', 'Bahman', 'Esfand'
  ];

  // Standard Kazimierz Borkowski / Behdad Esfahbod astronomical-aligned integer conversion
  function div(a, b) {
    return Math.trunc(a / b);
  }

  function g2d(gy, gm, gd) {
    let d = div((gy + div(gm - 8, 6) + 100100) * 1461, 4)
      + div(153 * ((gm + 9) % 12) + 2, 5)
      + gd - 34840408;
    d = d - div(div(gy + 100100 + div(gm - 8, 6), 100) * 3, 4) + 752;
    return d;
  }

  function d2g(jdn) {
    let j = 4 * jdn + 139361631;
    j = j + div(div(4 * jdn + 183187720, 146097) * 3, 4) * 4 - 3908;
    const i = div((j % 1461), 4) * 5 + 308;
    const gd = div((i % 153), 5) + 1;
    const gm = (div(i, 153) % 12) + 1;
    const gy = div(j, 1461) - 100100 + div(8 - gm, 6);
    return { gy, gm, gd };
  }

  function jalCal(jy) {
    const breaks = [
      -61, 9, 38, 199, 426, 686, 756, 818, 1111, 1181, 1210,
      1635, 2060, 2097, 2192, 2262, 2324, 2394, 2456, 3178
    ];
    let bl = breaks.length;
    let gy = jy + 621;
    let leapJ = -14;
    let jp = breaks[0];
    let jm, jump, leap, n, i;
    if (jy < jp || jy >= breaks[bl - 1]) {
      // Fallback 33-year cycle outside table bounds
      const cycle = ((jy % 33) + 33) % 33;
      const isLeapFallback = [1, 5, 9, 13, 17, 22, 26, 30].includes(cycle);
      return { leap: isLeapFallback ? 0 : 1, gy, march: 21 };
    }
    for (i = 1; i < bl; i += 1) {
      jm = breaks[i];
      jump = jm - jp;
      if (jy < jm) break;
      leapJ = leapJ + div(jump, 33) * 8 + div((jump % 33), 4);
      jp = jm;
    }
    n = jy - jp;
    leapJ = leapJ + div(n, 33) * 8 + div(((n % 33) + 3), 4);
    if ((jump % 33) === 4 && jump - n === 4) leapJ += 1;
    const leapG = div(gy, 4) - div((div(gy, 100) + 1) * 3, 4) - 150;
    const march = 20 + leapJ - leapG;
    if (jump - n < 6) n = n - jump + div(jump + 4, 33) * 33;
    leap = (((n + 1) % 33) - 1) % 4;
    if (leap === -1) leap = 4;
    return { leap, gy, march };
  }

  function j2d(jy, jm, jd) {
    const r = jalCal(jy);
    return g2d(r.gy, 3, r.march) + (jm - 1) * 31 - div(jm, 7) * (jm - 7) + jd - 1;
  }

  function d2j(jdn) {
    const gy = d2g(jdn).gy;
    let jy = gy - 621;
    const r = jalCal(jy);
    const jdn1f = g2d(gy, 3, r.march);
    let jd, jm, k;
    k = jdn - jdn1f;
    if (k >= 0) {
      if (k <= 185) {
        jm = 1 + div(k, 31);
        jd = (k % 31) + 1;
        return { jy, jm, jd };
      }
      k -= 186;
    } else {
      jy -= 1;
      k += 179;
      if (r.leap === 1) k += 1;
    }
    jm = 7 + div(k, 30);
    jd = (k % 30) + 1;
    return { jy, jm, jd };
  }

  function toJalali(gy, gm, gd) {
    return d2j(g2d(gy, gm, gd));
  }

  function toGregorian(jy, jm, jd) {
    return d2g(j2d(jy, jm, jd));
  }

  function monthName(jm, locale) {
    const idx = Math.max(0, Math.min(11, Number(jm) - 1));
    return String(locale || '').startsWith('fa') ? FA_MONTHS[idx] : EN_MONTHS[idx];
  }

  const NO_GROUP_FMT = new Map();
  function formatIntNoGroup(val, locale) {
    const loc = locale || 'en';
    if (!NO_GROUP_FMT.has(loc)) {
      NO_GROUP_FMT.set(loc, new Intl.NumberFormat(loc, { useGrouping: false }));
    }
    return NO_GROUP_FMT.get(loc).format(val);
  }

  function formatJalaliDate(dateObj, options = {}, locale = 'fa', numFmt = null) {
    const tz = options.timeZone || undefined;
    // Extract wall-clock calendar components in the requested timezone using en-US Intl
    const parts = {};
    try {
      const dtf = new Intl.DateTimeFormat('en-US', {
        timeZone: tz,
        year: 'numeric',
        month: 'numeric',
        day: 'numeric',
        hour: options.hour ? '2-digit' : undefined,
        minute: options.minute ? '2-digit' : undefined,
        hourCycle: options.hour ? (options.hourCycle || 'h23') : undefined,
        timeZoneName: options.timeZoneName || undefined
      });
      for (const p of dtf.formatToParts(dateObj)) {
        parts[p.type] = p.value;
      }
    } catch {
      parts.year = String(dateObj.getUTCFullYear());
      parts.month = String(dateObj.getUTCMonth() + 1);
      parts.day = String(dateObj.getUTCDate());
    }

    const gy = Number(parts.year);
    const gm = Number(parts.month);
    const gd = Number(parts.day);
    const j = toJalali(gy, gm, gd);
    const fmtInt = (val) => formatIntNoGroup(val, locale || 'en');
    const isFa = String(locale || '').startsWith('fa');

    const segs = [];
    if (options.weekday) {
      segs.push(new Intl.DateTimeFormat(locale, { weekday: options.weekday, timeZone: tz }).format(dateObj));
    }
    const wantDay = Boolean(options.day);
    const wantMonth = Boolean(options.month);
    const wantYear = Boolean(options.year);
    if (wantDay && wantMonth && wantYear) {
      segs.push(isFa
        ? `${fmtInt(j.jd)} ${monthName(j.jm, locale)} ${fmtInt(j.jy)}`
        : `${fmtInt(j.jd)} ${monthName(j.jm, locale)} ${fmtInt(j.jy)}`);
    } else if (wantDay && wantMonth) {
      segs.push(`${fmtInt(j.jd)} ${monthName(j.jm, locale)}`);
    } else if (wantMonth && wantYear) {
      segs.push(`${monthName(j.jm, locale)} ${fmtInt(j.jy)}`);
    } else if (wantMonth) {
      segs.push(monthName(j.jm, locale));
    } else if (wantYear) {
      segs.push(fmtInt(j.jy));
    } else if (wantDay) {
      segs.push(fmtInt(j.jd));
    }

    if (options.hour && parts.hour !== undefined) {
      const hh = parts.hour.padStart(2, '0');
      const mm = (parts.minute || '00').padStart(2, '0');
      const timeStr = isFa
        ? `${hh.replace(/\d/g, d => '۰۱۲۳۴۵۶۷۸۹'[Number(d)])}:${mm.replace(/\d/g, d => '۰۱۲۳۴۵۶۷۸۹'[Number(d)])}`
        : `${hh}:${mm}`;
      segs.push(timeStr + (parts.timeZoneName ? ` ${parts.timeZoneName}` : ''));
    }

    return segs.join(isFa ? '، ' : ', ');
  }

  return { toJalali, toGregorian, monthName, formatJalaliDate, FA_MONTHS, EN_MONTHS };
})();

if (typeof module !== 'undefined') module.exports = TypeCountJalali;
