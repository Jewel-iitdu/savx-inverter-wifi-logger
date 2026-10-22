// Thin uPlot wrapper: x in Unix seconds, local Asia/Dhaka axis (d1 §6.3),
// null = gap, colours from CSS tokens so light/dark both work.
import { useEffect, useRef } from 'preact/hooks';
import uPlot from 'uplot';
import 'uplot/dist/uPlot.min.css';
import { TZ } from '../lib/time';
import { getLang } from '../lib/i18n';

export interface SeriesSpec {
  label: string;
  color: string; // CSS variable name, e.g. '--c-pv'
  scale?: string;
  dash?: boolean;
  fill?: boolean;
  width?: number;
  unit?: string;
  digits?: number;
}

export interface AxisSpec {
  scale: string;
  side?: 'left' | 'right';
  label?: string;
  range?: [number | null, number | null];
}

export interface ChartProps {
  data: Array<Array<number | null>>; // [xs, ...series]
  series: SeriesSpec[];
  axes: AxisSpec[];
  height?: number;
  syncKey?: string;
  xRange?: [number, number];
  /** x values are seconds within a day mapped onto a date (compare view). */
  timeOnly?: boolean;
  themeKey: string;
}

const fmtHM = new Intl.DateTimeFormat('en-GB', { timeZone: TZ, hour: '2-digit', minute: '2-digit', hourCycle: 'h23' });
const fmtDHM = new Intl.DateTimeFormat('en-GB', { timeZone: TZ, day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit', hourCycle: 'h23' });
const fmtDM = new Intl.DateTimeFormat('en-GB', { timeZone: TZ, weekday: 'short', day: 'numeric' });

/** 24-hour local tick labels; day labels when the window spans several days. */
function xTicks(u: uPlot, splits: number[]): string[] {
  const span = (u.scales.x.max ?? 0) - (u.scales.x.min ?? 0);
  return splits.map((v) => (span > 2 * 86400 ? fmtDM : fmtHM).format(new Date(v * 1000)));
}

function cssVar(name: string): string {
  return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#888';
}

function withAlpha(color: string, alpha: number): string {
  const m = /^#([0-9a-f]{6})$/i.exec(color);
  if (!m) return color;
  const n = parseInt(m[1], 16);
  return `rgba(${(n >> 16) & 255},${(n >> 8) & 255},${n & 255},${alpha})`;
}

export function UChart(props: ChartProps) {
  const host = useRef<HTMLDivElement>(null);
  const plot = useRef<uPlot | null>(null);
  const height = props.height ?? 200;

  // (Re)create when the structure or the theme changes.
  const structKey = `${getLang()}|${props.themeKey}|${props.series.map((s) => s.label + s.color + (s.scale ?? '')).join(',')}|${props.axes
    .map((a) => a.scale + (a.side ?? ''))
    .join(',')}|${height}|${props.syncKey ?? ''}`;

  useEffect(() => {
    const el = host.current;
    if (!el) return;
    const text = cssVar('--muted');
    const grid = cssVar('--grid');
    const scales: uPlot.Scales = { x: { time: true } };
    for (const a of props.axes) {
      scales[a.scale] = a.range ? { range: (_u, min, max) => [a.range![0] ?? min, a.range![1] ?? max] } : { auto: true };
    }
    const opts: uPlot.Options = {
      width: Math.max(200, el.clientWidth),
      height,
      tzDate: (ts) => uPlot.tzDate(new Date(ts * 1e3), TZ),
      cursor: { sync: props.syncKey ? { key: props.syncKey } : undefined, drag: { x: false, y: false }, points: { size: 6 } },
      legend: { live: true },  // values appear while touching/hovering; idle legend = names only
      scales,
      series: [
        // no "Time:" label and no "–" placeholders while idle (owner: the dashed legend looked broken on the phone)
        { label: '\u200b', value: (_u, v) => (v == null ? '' : (props.timeOnly ? fmtHM : fmtDHM).format(new Date(v * 1000))) },  // '' would become uPlot's default "Time"
        ...props.series.map<uPlot.Series>((s) => {
          const c = cssVar(s.color);
          return {
            label: s.label,
            scale: s.scale ?? 'y',
            stroke: c,
            width: s.width ?? 1.5,
            dash: s.dash ? [5, 4] : undefined,
            fill: s.fill ? withAlpha(c, 0.12) : undefined,
            spanGaps: false,
            points: { show: false },
            value: (_u, v) => (v == null ? '' : `${v.toFixed(s.digits ?? 0)}${s.unit ? ' ' + s.unit : ''}`),
          };
        }),
      ],
      axes: [
        { stroke: text, grid: { stroke: grid, width: 1 }, ticks: { stroke: grid, width: 1 }, font: '11px system-ui, sans-serif', values: xTicks },
        ...props.axes.map<uPlot.Axis>((a) => ({
          scale: a.scale,
          side: a.side === 'right' ? 1 : 3,
          // units are drawn upright above the plot (see the .chart-units row): a rotated "V" read as "<"
          labelSize: 0,
          stroke: text,
          grid: { show: a.side !== 'right', stroke: grid, width: 1 },
          ticks: { stroke: grid, width: 1 },
          font: '11px system-ui, sans-serif',
          size: 44,
        })),
      ],
    };
    const u = new uPlot(opts, props.data as uPlot.AlignedData, el);
    plot.current = u;
    if (props.xRange) u.setScale('x', { min: props.xRange[0], max: props.xRange[1] });
    const ro = new ResizeObserver(() => {
      const w = el.clientWidth;
      if (w > 0 && Math.abs(w - u.width) > 2) u.setSize({ width: w, height });
    });
    ro.observe(el);
    return () => {
      ro.disconnect();
      u.destroy();
      plot.current = null;
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [structKey]);

  useEffect(() => {
    const u = plot.current;
    if (!u) return;
    u.setData(props.data as uPlot.AlignedData, !props.xRange);
    if (props.xRange) u.setScale('x', { min: props.xRange[0], max: props.xRange[1] });
  }, [props.data, props.xRange?.[0], props.xRange?.[1]]);

  const left = props.axes.find((a) => a.side !== 'right')?.label;
  const right = props.axes.find((a) => a.side === 'right')?.label;
  return (
    <div class="chart-box">
      {(left || right) && (
        <div class="chart-units">
          <span>{left ?? ''}</span>
          <span>{right ?? ''}</span>
        </div>
      )}
      <div class="chart" ref={host} style={{ minHeight: `${height + 40}px` }} />
    </div>
  );
}
