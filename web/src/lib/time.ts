// Date keys are the LOCAL Asia/Dhaka date 'YYYY-MM-DD'; minute keys are local
// 'hhmm'; timestamps are UTC epoch seconds (d1 §4.1). Bangladesh has no DST,
// but offsets are computed with Intl so nothing here assumes +06:00.

import { isBn, localizeDigits, t } from './i18n';

export const TZ = 'Asia/Dhaka';

const partsFmt = new Intl.DateTimeFormat('en-GB', {
  timeZone: TZ,
  year: 'numeric',
  month: '2-digit',
  day: '2-digit',
  hour: '2-digit',
  minute: '2-digit',
  second: '2-digit',
  hourCycle: 'h23',
});

export interface Parts {
  y: number;
  m: number;
  d: number;
  hh: number;
  mm: number;
  ss: number;
}

export function nowSec(): number {
  return Math.floor(Date.now() / 1000);
}

export function localParts(epochSec: number): Parts {
  const p: Record<string, number> = {};
  for (const part of partsFmt.formatToParts(new Date(epochSec * 1000))) {
    if (part.type !== 'literal') p[part.type] = Number(part.value);
  }
  return { y: p.year, m: p.month, d: p.day, hh: p.hour % 24, mm: p.minute, ss: p.second };
}

const pad2 = (n: number) => String(n).padStart(2, '0');

export function dateKey(epochSec: number = nowSec()): string {
  const p = localParts(epochSec);
  return `${p.y}-${pad2(p.m)}-${pad2(p.d)}`;
}

export function hhmmKey(epochSec: number): string {
  const p = localParts(epochSec);
  return pad2(p.hh) + pad2(p.mm);
}

export function todayKey(): string {
  return dateKey(nowSec());
}

/** Offset of TZ from UTC in seconds at the given instant. */
function tzOffsetSec(epochSec: number): number {
  const p = localParts(epochSec);
  const asUtc = Date.UTC(p.y, p.m - 1, p.d, p.hh, p.mm, p.ss) / 1000;
  return asUtc - epochSec;
}

/** Local date key + hhmm -> UTC epoch seconds. */
export function keyToEpoch(date: string, hhmm = '0000'): number {
  const [y, m, d] = date.split('-').map(Number);
  const hh = Number(hhmm.slice(0, 2));
  const mm = Number(hhmm.slice(2, 4));
  const guess = Date.UTC(y, m - 1, d, hh, mm) / 1000;
  const off = tzOffsetSec(guess);
  return guess - off;
}

export function minuteOfDay(hhmm: string): number {
  return Number(hhmm.slice(0, 2)) * 60 + Number(hhmm.slice(2, 4));
}

export function hhmmFromMinute(min: number): string {
  return pad2(Math.floor(min / 60)) + pad2(min % 60);
}

/** Calendar arithmetic on date keys (pure date math, no time zone). */
export function addDays(date: string, n: number): string {
  const [y, m, d] = date.split('-').map(Number);
  const t = new Date(Date.UTC(y, m - 1, d + n));
  return `${t.getUTCFullYear()}-${pad2(t.getUTCMonth() + 1)}-${pad2(t.getUTCDate())}`;
}

export function daysBetween(a: string, b: string): number {
  const [y1, m1, d1] = a.split('-').map(Number);
  const [y2, m2, d2] = b.split('-').map(Number);
  return Math.round((Date.UTC(y2, m2 - 1, d2) - Date.UTC(y1, m1 - 1, d1)) / 86400000);
}

/** Inclusive list of date keys from a to b. */
export function dateRange(a: string, b: string): string[] {
  const out: string[] = [];
  const n = daysBetween(a, b);
  for (let i = 0; i <= n; i++) out.push(addDays(a, i));
  return out;
}

export function monthKey(date: string): string {
  return date.slice(0, 7);
}

export function monthBounds(ym: string): [string, string] {
  const [y, m] = ym.split('-').map(Number);
  const last = new Date(Date.UTC(y, m, 0)).getUTCDate();
  return [`${ym}-01`, `${ym}-${pad2(last)}`];
}

export function addMonths(ym: string, n: number): string {
  const [y, m] = ym.split('-').map(Number);
  const t = new Date(Date.UTC(y, m - 1 + n, 1));
  return `${t.getUTCFullYear()}-${pad2(t.getUTCMonth() + 1)}`;
}

const dayFmt = new Intl.DateTimeFormat('en-GB', { timeZone: 'UTC', weekday: 'short', day: 'numeric', month: 'short' });
const dayMonthFmt = new Intl.DateTimeFormat('en-GB', { timeZone: 'UTC', day: 'numeric', month: 'short' });
const monthFmt = new Intl.DateTimeFormat('en-GB', { timeZone: 'UTC', month: 'long', year: 'numeric' });

// Bangla calendar words (Gregorian months, as used in Bangladesh). Hand-written so the output does not
// depend on the browser's ICU data.
const BN_MONTHS = ['জানুয়ারি', 'ফেব্রুয়ারি', 'মার্চ', 'এপ্রিল', 'মে', 'জুন', 'জুলাই', 'আগস্ট', 'সেপ্টেম্বর', 'অক্টোবর', 'নভেম্বর', 'ডিসেম্বর'];
const BN_MONTHS_SHORT = ['জানু', 'ফেব্রু', 'মার্চ', 'এপ্রিল', 'মে', 'জুন', 'জুলাই', 'আগস্ট', 'সেপ্টে', 'অক্টো', 'নভে', 'ডিসে'];
const BN_MONTHS_TINY = ['জা', 'ফে', 'মা', 'এ', 'মে', 'জুন', 'জুল', 'আ', 'সে', 'অ', 'ন', 'ডি'];
const EN_WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const BN_WEEKDAYS = ['রবি', 'সোম', 'মঙ্গল', 'বুধ', 'বৃহঃ', 'শুক্র', 'শনি'];

/** Short weekday name, 0 = Sunday. */
export function weekdayShort(i: number): string {
  return (isBn() ? BN_WEEKDAYS : EN_WEEKDAYS)[i] ?? '';
}

/** Bangla part of the day for a 0-23 hour: রাত ভোর সকাল দুপুর বিকাল সন্ধ্যা. */
function bnDayPart(hh: number): string {
  if (hh < 4) return 'রাত';
  if (hh < 6) return 'ভোর';
  if (hh < 12) return 'সকাল';
  if (hh < 15) return 'দুপুর';
  if (hh < 18) return 'বিকাল';
  if (hh < 20) return 'সন্ধ্যা';
  return 'রাত';
}

const NB = '\u00a0';
const ampm = (hh: number) => (hh < 12 ? 'AM' : 'PM');

/** Wall-clock time, 12-hour: '7:11 AM' in English (owner's request); 'সকাল ৭:১১' (with the part of the day) in Bangla. */
export function fmtClock(hh: number, mm: number): string {
  if (!isBn()) return `${hh % 12 || 12}:${pad2(mm)}${NB}${ampm(hh)}`;
  return `${bnDayPart(hh)} ${localizeDigits(`${hh % 12 || 12}:${pad2(mm)}`)}`;
}

/** Chart tick: '7 AM', '7:30 AM' in English; Bangla keeps the compact 24-hour '07:00' (narrow axis). */
export function fmtTick(epochSec: number): string {
  const p = localParts(epochSec);
  if (isBn()) return `${pad2(p.hh)}:${pad2(p.mm)}`;
  return p.mm ? fmtClock(p.hh, p.mm) : `${p.hh % 12 || 12}${NB}${ampm(p.hh)}`;
}

/** 24-hour '22 Oct, 07:11': the logger's e-mails still use it, so the e-mail preview does too. */
export function fmtDateTime24(epochSec: number): string {
  const p = localParts(epochSec);
  return `${dayMonthFmt.format(new Date(Date.UTC(p.y, p.m - 1, p.d)))}, ${pad2(p.hh)}:${pad2(p.mm)}`;
}

const ymd = (date: string) => date.split('-').map(Number);

export function fmtTime(epochSec: number): string {
  const p = localParts(epochSec);
  return fmtClock(p.hh, p.mm);
}

/** '22 Oct, 7:11 AM' ('২২ অক্টো, সকাল ৭:১১' in Bangla). */
export function fmtDateTime(epochSec: number): string {
  const p = localParts(epochSec);
  if (!isBn()) return `${dayMonthFmt.format(new Date(Date.UTC(p.y, p.m - 1, p.d)))}, ${fmtClock(p.hh, p.mm)}`;
  return `${localizeDigits(String(p.d))} ${BN_MONTHS_SHORT[p.m - 1]}, ${fmtClock(p.hh, p.mm)}`;
}

/** 'Tue 21 Oct' for a date key ('মঙ্গল, ২১ অক্টো' in Bangla). */
export function fmtDay(date: string): string {
  const [y, m, d] = ymd(date);
  const dt = new Date(Date.UTC(y, m - 1, d));
  if (!isBn()) return dayFmt.format(dt);
  return `${BN_WEEKDAYS[dt.getUTCDay()]}, ${localizeDigits(String(d))} ${BN_MONTHS_SHORT[m - 1]}`;
}

/** '21 Oct' for a date key ('২১ অক্টো' in Bangla). */
export function fmtDayMonth(date: string): string {
  const [y, m, d] = ymd(date);
  if (!isBn()) return dayMonthFmt.format(new Date(Date.UTC(y, m - 1, d)));
  return `${localizeDigits(String(d))} ${BN_MONTHS_SHORT[m - 1]}`;
}

/** 'October 2026' ('অক্টোবর ২০২৬'). */
export function fmtMonth(ym: string): string {
  const [y, m] = ymd(ym);
  if (!isBn()) return monthFmt.format(new Date(Date.UTC(y, m - 1, 1)));
  return `${BN_MONTHS[m - 1]} ${localizeDigits(String(y))}`;
}

/** 'Oct' ('অক্টো'), for table rows. */
export function fmtMonthShort(ym: string): string {
  const m = ymd(ym)[1];
  return isBn() ? BN_MONTHS_SHORT[m - 1] : fmtMonth(ym).split(' ')[0].slice(0, 3);
}

/** 'O' ('অ'), for the 12 bar labels of a year chart. */
export function fmtMonthTiny(ym: string): string {
  const m = ymd(ym)[1];
  return isBn() ? BN_MONTHS_TINY[m - 1] : fmtMonth(ym).slice(0, 1);
}

/** A date key for display: '2026-10-22' ('২০২৬-১০-২২' in Bangla). */
export function fmtDateKey(date: string): string {
  return localizeDigits(date);
}

export function fmtDuration(sec: number): string {
  if (!Number.isFinite(sec) || sec < 0) return '–';
  if (sec < 60) return t('u.s', { n: Math.round(sec) });
  const m = Math.round(sec / 60);
  if (m < 60) return t('u.min', { n: m });
  const h = Math.floor(m / 60);
  const r = m % 60;
  if (h < 48) return r ? t('u.hmin', { h, m: r }) : t('u.h', { n: h });
  return t('u.d', { n: Math.round(h / 24) });
}

export function fmtAgo(sec: number): string {
  if (!Number.isFinite(sec)) return t('common.never');
  if (sec < 5) return t('time.justNow');
  return t('time.ago', { d: fmtDuration(sec) });
}
