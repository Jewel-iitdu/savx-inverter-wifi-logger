import { useMemo } from 'preact/hooks';
import { UChart } from './UChart';
import type { DaySeries } from '../lib/series';
import { t } from '../lib/i18n';

/**
 * The three standard charts with a shared cursor (d1 §6.2 Today):
 * (1) PV / Load / Battery W, (2) SOC % and Battery V, (3) Grid V and Output V.
 */
export function DayCharts(props: { s: DaySeries; themeKey: string; xRange?: [number, number]; syncKey: string; timeOnly?: boolean; va?: boolean }) {
  const { s } = props;
  const lu = props.va ? 'VA' : 'W'; // load unit (outputIsVA); PV and battery are DC, always W
  const power = useMemo(() => [s.xs, s.pv, s.load, s.batt], [s]);
  const batt = useMemo(() => [s.xs, s.soc, s.bv], [s]);
  const volts = useMemo(() => [s.xs, s.gv, s.ov], [s]);
  return (
    <div class="charts">
      <h3 class="chart-t">{t('w.power')}</h3>
      <UChart
        data={power}
        themeKey={props.themeKey}
        syncKey={props.syncKey}
        xRange={props.xRange}
        timeOnly={props.timeOnly}
        series={[
          { label: 'PV', color: '--c-pv', unit: 'W', fill: true },
          { label: t('w.load'), color: '--c-load', unit: lu },
          { label: t('w.battery'), color: '--c-batt', unit: 'W' },
        ]}
        axes={[{ scale: 'y', label: props.va ? 'W / VA' : 'W' }]}
      />
      <h3 class="chart-t">{t('w.battery')}</h3>
      <UChart
        data={batt}
        themeKey={props.themeKey}
        syncKey={props.syncKey}
        xRange={props.xRange}
        timeOnly={props.timeOnly}
        height={160}
        series={[
          { label: 'SOC', color: '--c-batt', unit: '%', scale: '%', fill: true },
          { label: t('chart.batteryV'), color: '--c-volt', unit: 'V', scale: 'V', digits: 2 },
        ]}
        axes={[
          { scale: '%', label: '%', range: [0, 100] },
          { scale: 'V', side: 'right', label: 'V' },
        ]}
      />
      <h3 class="chart-t">{t('chart.acVoltage')}</h3>
      <UChart
        data={volts}
        themeKey={props.themeKey}
        syncKey={props.syncKey}
        xRange={props.xRange}
        timeOnly={props.timeOnly}
        height={140}
        series={[
          { label: t('chart.gridV'), color: '--c-grid', unit: 'V', digits: 1 },
          { label: t('chart.outputV'), color: '--c-out', unit: 'V', digits: 1 },
        ]}
        axes={[{ scale: 'y', label: 'V' }]}
      />
    </div>
  );
}
