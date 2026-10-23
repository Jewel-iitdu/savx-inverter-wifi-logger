// Power-flow card: PV -> inverter -> load, battery <->, grid -> (d1 §6.2).
// Lines animate in the direction of flow; static when reduced motion is set.
import { BATT_STANDBY_W, fmtPower, fmtW, isNum } from '../lib/format';
import type { Live } from '../data/types';
import { localizeDigits as dg, t } from '../lib/i18n';

interface FlowProps {
  live: Live | null;
  stale: boolean;
  va?: boolean; // load and grid in VA (outputIsVA); solar and battery are DC, always W
  gridVa?: boolean; // grid in VA (default: va); false when the grid power is the meter's real W (fw 0.9.0)
  loadVa?: boolean; // load in VA (default: va); false when the load is worked out from the meter (line mode)
}

// The value sits on a chord about 46 units wide inside the r=27 circle: "415 W" gets 13.5, "1.30 kVA" about 9.6, and
// textLength squeezes anything still too wide, so long values never spill out of the circle (owner screenshot 10:50).
const VAL_MAX_W = 46;
function valProps(s: string) {
  const n = Math.max([...s].length, 1);
  const fs = Math.max(8.5, Math.min(13.5, VAL_MAX_W / (0.6 * n)));
  const tooWide = 0.6 * fs * n > VAL_MAX_W;
  return { style: { fontSize: `${fs.toFixed(1)}px` }, ...(tooWide ? { textLength: VAL_MAX_W, lengthAdjust: 'spacingAndGlyphs' as const } : {}) };
}

function Node(props: { x: number; y: number; label: string; value: string; sub?: string; color: string; icon: string }) {
  return (
    <g transform={`translate(${props.x},${props.y})`}>
      <circle r="27" class="pf-node" style={{ stroke: `var(${props.color})` }} />
      <path d={props.icon} class="pf-icon" style={{ stroke: `var(${props.color})` }} transform="translate(-10,-17) scale(0.85)" />
      <text y="12" class="pf-val" {...valProps(props.value)}>
        {props.value}
      </text>
      <text y="44" class="pf-label">{props.label}</text>
      {props.sub && (
        <text y="57" class="pf-sub">
          {props.sub}
        </text>
      )}
    </g>
  );
}

function Line(props: { d: string; active: boolean; reverse?: boolean; color: string }) {
  return (
    <g>
      <path d={props.d} class="pf-track" />
      {props.active && <path d={props.d} class={`pf-flow ${props.reverse ? 'rev' : ''}`} style={{ stroke: `var(${props.color})` }} />}
    </g>
  );
}

// 24x24 stroke icons (simplified, hand-drawn paths)
const ICON_SUN = 'M12 7a5 5 0 1 0 0.01 0M12 1v3M12 20v3M1 12h3M20 12h3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1';
const ICON_GRID = 'M8 22l4-20 4 20M6 8h12M5 14h14M9 14l6 8M15 14l-6 8';
const ICON_HOME = 'M3 11l9-8 9 8M5 9v12h14V9M10 21v-6h4v6';
const ICON_BATT = 'M3 7h16v10H3zM21 10v4M6 10v4M9 10v4';
const ICON_INV = 'M4 4h16v16H4zM8 12c1.3-2.6 2.7-2.6 4 0s2.7 2.6 4 0';

export function PowerFlow({ live, stale, va = false, gridVa = va, loadVa = va }: FlowProps) {
  const pv = live?.pvw;
  const load = live?.w;
  const bw = live?.bw;
  const gv = live?.gv;
  const gw = live?.gw;
  const gridPresent = isNum(gv) && gv > 90;
  const onGrid = live?.md === 'L' || live?.md === 'Y';
  const thr = 15;
  const cls = `pf ${stale ? 'pf-stale' : ''}`;
  return (
    <svg class={cls} viewBox="0 0 340 250" role="img" aria-label={t('pf.aria')}>
      {/* inverter at (170,118) */}
      <Line d="M 70 52 Q 120 52 150 100" active={isNum(pv) && pv > thr} color="--c-pv" />
      <Line d="M 70 184 Q 120 184 150 136" active={gridPresent && onGrid} color="--c-grid" />
      <Line d="M 192 118 L 268 118" active={isNum(load) && load > thr} color="--c-load" />
      <Line d="M 170 142 L 170 196" active={isNum(bw) && Math.abs(bw) > BATT_STANDBY_W} reverse={isNum(bw) && bw < 0} color="--c-batt" />
      <Node x={48} y={52} label={t('w.solar')} value={fmtW(pv, 1)} sub={isNum(live?.pvv) ? dg(`${live!.pvv!.toFixed(0)} V`) : undefined} color="--c-pv" icon={ICON_SUN} />
      <Node
        x={48}
        y={184}
        label={t('w.grid')}
        value={gridPresent ? (isNum(gw) ? fmtPower(gw, gridVa, 1) : dg(`${gv!.toFixed(0)} V`)) : t('common.Off')}
        sub={gridPresent ? `${dg(`${gv!.toFixed(0)} V`)} · ${onGrid ? t('pf.inUse') : t('pf.standby')}` : t('pf.noGrid')}
        color="--c-grid"
        icon={ICON_GRID}
      />
      <g transform="translate(170,118)">
        <rect x="-22" y="-22" width="44" height="44" rx="10" class="pf-node" style={{ stroke: 'var(--fg)' }} />
        <path d={ICON_INV} class="pf-icon" style={{ stroke: 'var(--fg)' }} transform="translate(-10,-10) scale(0.85)" />
      </g>
      <Node x={292} y={118} label={t('w.load')} value={fmtPower(load, loadVa, 1)} sub={isNum(live?.ld) ? dg(`${live!.ld}%`) : undefined} color="--c-load" icon={ICON_HOME} />
      <Node
        x={170}
        y={214}
        label=""
        value={fmtW(isNum(bw) ? Math.abs(bw) : undefined, 1)}
        color="--c-batt"
        icon={ICON_BATT}
      />
      <text x="212" y="214" class="pf-side">
        {isNum(bw) ? (bw > BATT_STANDBY_W ? t('pf.charging') : bw < -BATT_STANDBY_W ? t('pf.discharging') : t('pf.standby')) : ''}
      </text>
      <text x="212" y="229" class="pf-side">
        {t('w.battery')}
      </text>
    </svg>
  );
}
