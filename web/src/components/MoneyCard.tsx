// Electricity cost of one day: what the grid cost, what solar saved, and the billing month up to that day.
import { t } from '../lib/i18n';
import { fmtNum, fmtRate, fmtTk } from '../lib/format';
import type { DayMoney } from '../lib/money';
import { rateAt, sumMoney, ZERO } from '../lib/tariff';
import { Card, Spinner, Tile } from './ui';

const kwh = (v: number) => fmtNum(v, v < 10 ? 2 : 1, 'kWh');

export function MoneyCard(props: { title: string; date: string; m: DayMoney }) {
  const { money, monthStart, cycleOf } = props.m;
  const day = money?.[props.date] ?? ZERO;
  const mtd = money ? sumMoney(money, cycleOf(props.date), props.date) : ZERO;
  return (
    <Card title={props.title}>
      {!money ? (
        <Spinner />
      ) : (
        <div class="tiles">
          <Tile label={t('money.saved')} value={fmtTk(day.savedTk)} sub={t('money.subSolar', { e: kwh(day.solar) })} tone="pv" />
          <Tile label={t('money.grid')} value={fmtTk(day.gridTk)} sub={t('money.subGrid', { e: kwh(day.grid) })} tone="grid" />
          <Tile label={t('money.savedMtd')} value={fmtTk(mtd.savedTk)} sub={t('money.subSolar', { e: kwh(mtd.solar) })} tone="pv" />
          <Tile label={t('money.billMtd')} value={fmtTk(mtd.gridTk)} sub={t('money.subRate', { e: kwh(mtd.grid), r: fmtRate(rateAt(mtd.grid)) })} tone="grid" />
        </div>
      )}
      <p class="muted small">{t('money.note', { n: monthStart ?? 1 })}</p>
    </Card>
  );
}
