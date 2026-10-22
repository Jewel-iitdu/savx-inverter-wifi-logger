// Grouped bar chart in plain SVG (d1 §6.1: no chart library for bars).
// Tap a group to see its values.
import { useState } from 'preact/hooks';
import { t } from '../lib/i18n';

export interface BarSeries {
  name: string;
  color: string; // CSS variable
  values: Array<number | null>;
}

export function Bars(props: { labels: string[]; series: BarSeries[]; unit: string; digits?: number; height?: number; tickEvery?: number }) {
  const [sel, setSel] = useState<number | null>(null);
  const n = props.labels.length;
  const h = props.height ?? 180;
  const W = 340;
  const padL = 34;
  const padB = 20;
  const plotW = W - padL - 4;
  const plotH = h - padB - 8;
  const max = Math.max(0.001, ...props.series.flatMap((s) => s.values.map((v) => v ?? 0)));
  // round tick steps (1/2/5 x 10^k): 0, 2, 4, 6, 8, 10, never 3/5/8 from rounded quarters
  const step = niceStep(max / 5);
  const nice = Math.max(step, Math.ceil(max / step) * step);
  const groupW = plotW / Math.max(1, n);
  const barW = Math.max(1, (groupW * 0.8) / props.series.length);
  const every = props.tickEvery ?? (n > 16 ? 5 : 1);
  const ticks: number[] = [];
  for (let v = 0; v <= nice + step / 2; v += step) ticks.push(v);
  const digits = props.digits ?? 1;
  return (
    <div class="bars">
      <div class="chart-units">
        <span>{props.unit}</span>
        <span />
      </div>
      <svg viewBox={`0 0 ${W} ${h}`} role="img" aria-label={t('bars.aria', { names: props.series.map((s) => s.name).join(', ') })}>
        {ticks.map((t) => {
          const y = 8 + plotH - (t / nice) * plotH;
          return (
            <g key={t}>
              <line x1={padL} x2={W - 4} y1={y} y2={y} class="b-grid" />
              <text x={padL - 4} y={y + 3} class="b-tick" text-anchor="end">
                {step >= 1 || t === 0 ? t.toFixed(0) : t.toFixed(1)}
              </text>
            </g>
          );
        })}
        {props.labels.map((label, i) => {
          const x0 = padL + i * groupW + groupW * 0.1;
          return (
            <g key={label} class={sel === i ? 'b-sel' : ''} onClick={() => setSel(sel === i ? null : i)}>
              <rect x={padL + i * groupW} y={8} width={groupW} height={plotH} class="b-hit" />
              {props.series.map((s, k) => {
                const v = s.values[i];
                if (v === null || v === undefined) return null;
                const bh = (v / nice) * plotH;
                return <rect key={s.name} x={x0 + k * barW} y={8 + plotH - bh} width={barW} height={Math.max(0, bh)} rx={Math.min(2, barW / 3)} style={{ fill: `var(${s.color})` }} />;
              })}
              {(i % every === 0 || i === n - 1) && (
                <text x={padL + i * groupW + groupW / 2} y={h - 6} class="b-tick" text-anchor="middle">
                  {label}
                </text>
              )}
            </g>
          );
        })}
      </svg>
      <div class="legend">
        {props.series.map((s) => (
          <span key={s.name}>
            <i style={{ background: `var(${s.color})` }} />
            {s.name}
            {sel !== null && s.values[sel] != null ? `: ${s.values[sel]!.toFixed(digits)} ${props.unit}` : ''}
          </span>
        ))}
        {sel !== null && <span class="muted">({props.labels[sel]})</span>}
      </div>
    </div>
  );
}

function niceStep(v: number): number {
  const p = Math.pow(10, Math.floor(Math.log10(Math.max(v, 1e-6))));
  for (const m of [1, 2, 5, 10]) if (m * p >= v) return m * p;
  return 10 * p;
}
