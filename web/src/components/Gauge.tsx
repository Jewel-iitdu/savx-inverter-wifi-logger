import { isNum } from '../lib/format';
import { localizeDigits as dg, t } from '../lib/i18n';

/** SOC ring gauge. */
export function Gauge(props: { value: number | undefined; label: string; sub?: string; lowAt?: number }) {
  const v = isNum(props.value) ? Math.max(0, Math.min(100, props.value)) : null;
  const r = 42;
  const c = 2 * Math.PI * r;
  const arc = 0.75; // 270° sweep
  const len = c * arc;
  const fill = v === null ? 0 : (len * v) / 100;
  const tone = v === null ? 'muted' : v <= (props.lowAt ?? 30) ? 'bad' : v <= (props.lowAt ?? 30) + 20 ? 'warn' : 'ok';
  return (
    <svg class="gauge" viewBox="0 0 110 100" role="img" aria-label={t('gauge.aria', { label: props.label, v: v === null ? t('gauge.unknown') : Math.round(v) })}>
      <g transform="translate(55,52) rotate(135)">
        <circle r={r} class="g-track" stroke-dasharray={`${len} ${c}`} />
        <circle r={r} class={`g-fill tone-${tone}`} stroke-dasharray={`${fill} ${c}`} />
      </g>
      <text x="55" y="55" class="g-val">
        {v === null ? '–' : dg(`${Math.round(v)}%`)}
      </text>
      <text x="55" y="72" class="g-label">
        {props.label}
      </text>
      {props.sub && (
        <text x="55" y="96" class="g-sub">
          {props.sub}
        </text>
      )}
    </svg>
  );
}
