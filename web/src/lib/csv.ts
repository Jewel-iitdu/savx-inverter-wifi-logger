// CSV export. On iPhone the file goes through the share sheet ("Save to
// Files") when the Web Share API accepts files, otherwise a Blob download.
// Call these synchronously from a tap handler with data already in memory,
// so Safari keeps the user gesture.

export type CsvValue = string | number | boolean | null | undefined;

function cell(v: CsvValue): string {
  if (v === null || v === undefined) return '';
  if (typeof v === 'number') return Number.isFinite(v) ? String(v) : '';
  const s = String(v);
  return /[",\n\r]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
}

export function toCsv(header: string[], rows: CsvValue[][]): string {
  return [header.map(cell).join(','), ...rows.map((r) => r.map(cell).join(','))].join('\r\n') + '\r\n';
}

export async function saveCsv(filename: string, text: string): Promise<'shared' | 'downloaded' | 'cancelled'> {
  const blob = new Blob([text], { type: 'text/csv;charset=utf-8' });
  const nav = navigator as Navigator & { canShare?: (d: ShareData) => boolean };
  try {
    const file = new File([blob], filename, { type: 'text/csv' });
    if (nav.share && nav.canShare && nav.canShare({ files: [file] })) {
      await nav.share({ files: [file], title: filename });
      return 'shared';
    }
  } catch (err) {
    if (err instanceof DOMException && err.name === 'AbortError') return 'cancelled';
    // fall through to a plain download
  }
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.rel = 'noopener';
  document.body.appendChild(a);
  a.click();
  a.remove();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
  return 'downloaded';
}
