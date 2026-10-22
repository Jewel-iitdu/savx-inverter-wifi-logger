// test_mtd - billing-cycle (month-to-date) sums for `month` rules (research/r8-email-alerts.md §4.5, §4.13).
#include <string.h>
#include <unity.h>

#include "agg/MonthToDate.h"
#include "model/TimeUtil.h"

void setUp() {}
void tearDown() {}

static int32_t D(int y, unsigned m, unsigned d) { return tu::daysFromCivil(y, m, d); }

static mrec::Daily day(int32_t dn, double egr, int n = 1440, int gout = 0) {
  mrec::Daily d;
  mrec::dailyReset(d, dn);
  d.egr = egr;
  d.eld = egr * 2;
  d.epv = 1000;
  d.n = n;
  d.gout = gout;
  return d;
}

void test_cycle_start_across_month_and_year_ends() {
  TEST_ASSERT_EQUAL(D(2026, 10, 1), MonthToDate::cycleStart(D(2026, 10, 21), 1));
  TEST_ASSERT_EQUAL(D(2026, 10, 1), MonthToDate::cycleStart(D(2026, 10, 1), 1));
  TEST_ASSERT_EQUAL(D(2026, 10, 5), MonthToDate::cycleStart(D(2026, 10, 21), 5));
  TEST_ASSERT_EQUAL(D(2026, 9, 5), MonthToDate::cycleStart(D(2026, 10, 4), 5));
  TEST_ASSERT_EQUAL(D(2026, 12, 28), MonthToDate::cycleStart(D(2027, 1, 10), 28));  // year end
  TEST_ASSERT_EQUAL(D(2027, 1, 28), MonthToDate::cycleStart(D(2027, 2, 27), 28));
  TEST_ASSERT_EQUAL(D(2027, 2, 28), MonthToDate::cycleStart(D(2027, 3, 1), 28));
  TEST_ASSERT_EQUAL(D(2026, 12, 5), MonthToDate::cycleStart(D(2027, 1, 4), 5));
  TEST_ASSERT_EQUAL_UINT32(20261005, MonthToDate::yyyymmdd(D(2026, 10, 5)));
}

void test_fold_is_idempotent_and_bounded_to_the_cycle() {
  MonthToDate m;
  int32_t today = D(2026, 10, 21);
  TEST_ASSERT_TRUE(m.setStartDay(1, today));
  TEST_ASSERT_EQUAL(D(2026, 10, 1), m.cycleDay());
  TEST_ASSERT_EQUAL_UINT32(20261001, m.cycleKey());
  TEST_ASSERT_TRUE(m.fold(day(D(2026, 10, 3), 1200)));
  TEST_ASSERT_FALSE(m.fold(day(D(2026, 10, 3), 1200)));  // the same day twice: ignored
  TEST_ASSERT_TRUE(m.fold(day(D(2026, 10, 2), 800)));     // out of order is fine
  TEST_ASSERT_FALSE(m.fold(day(D(2026, 9, 30), 999)));    // previous cycle
  TEST_ASSERT_FALSE(m.fold(day(D(2026, 11, 1), 999)));    // next cycle
  TEST_ASSERT_EQUAL_FLOAT(2000, (float)m.sum(rules::Metric::EGR));
  TEST_ASSERT_EQUAL_FLOAT(4000, (float)m.sum(rules::Metric::ELD));
  TEST_ASSERT_EQUAL(2, m.blob().days);
}

void test_gap_list_coverage_and_bootstrap_flag() {
  MonthToDate m;
  int32_t today = D(2026, 10, 6);
  m.setStartDay(1, today);
  m.fold(day(D(2026, 10, 1), 100));
  m.fold(day(D(2026, 10, 2), 100, 720));
  m.fold(day(D(2026, 10, 4), 100, 1440, 30));
  int32_t miss[8];
  int n = m.missing(today, miss, 8);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL(D(2026, 10, 3), miss[0]);
  TEST_ASSERT_EQUAL(D(2026, 10, 5), miss[1]);
  // 3600 of 5 x 1440 minutes = 50 %
  TEST_ASSERT_EQUAL(50, m.coveragePct(today));
  TEST_ASSERT_EQUAL_FLOAT(30, (float)m.sum(rules::Metric::GOUT));
  TEST_ASSERT_TRUE(m.needBootstrap(today));
  m.markBootstrapped();
  TEST_ASSERT_FALSE(m.needBootstrap(today));
  m.fold(day(D(2026, 10, 3), 50));  // gap filled later (file or RTDB)
  m.fold(day(D(2026, 10, 5), 50));
  TEST_ASSERT_EQUAL(0, m.missing(today, nullptr, 0));
  TEST_ASSERT_EQUAL_FLOAT(400, (float)m.sum(rules::Metric::EGR));
  // first day of a cycle: nothing finished yet = 100 %
  MonthToDate f;
  f.setStartDay(1, D(2026, 11, 1));
  TEST_ASSERT_EQUAL(100, f.coveragePct(D(2026, 11, 1)));
  TEST_ASSERT_FALSE(f.needBootstrap(D(2026, 11, 1)));
}

void test_roll_and_monthstart_change_reset() {
  MonthToDate m;
  m.setStartDay(5, D(2026, 10, 21));
  m.fold(day(D(2026, 10, 10), 500));
  TEST_ASSERT_FALSE(m.roll(D(2026, 11, 4)));  // still the 5 Oct cycle
  TEST_ASSERT_TRUE(m.roll(D(2026, 11, 5)));   // a new cycle starts
  TEST_ASSERT_EQUAL_FLOAT(0, (float)m.sum(rules::Metric::EGR));
  TEST_ASSERT_EQUAL_UINT32(20261105, m.cycleKey());
  m.fold(day(D(2026, 11, 6), 700));
  TEST_ASSERT_TRUE(m.setStartDay(1, D(2026, 11, 20)));  // owner changes monthStart: the cycle restarts
  TEST_ASSERT_EQUAL_UINT32(20261101, m.cycleKey());
  TEST_ASSERT_EQUAL_FLOAT(0, (float)m.sum(rules::Metric::EGR));
  TEST_ASSERT_FALSE(m.setStartDay(1, D(2026, 11, 20)));  // unchanged
  // the blob round-trips (NVS energy/mtd)
  m.fold(day(D(2026, 11, 2), 321));
  MtdBlob b = m.blob();
  MonthToDate m2;
  m2.load(b);
  TEST_ASSERT_TRUE(m2.folded(D(2026, 11, 2)));
  TEST_ASSERT_FALSE(m2.fold(day(D(2026, 11, 2), 321)));
  TEST_ASSERT_EQUAL_FLOAT(321, (float)m2.sum(rules::Metric::EGR));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cycle_start_across_month_and_year_ends);
  RUN_TEST(test_fold_is_idempotent_and_bounded_to_the_cycle);
  RUN_TEST(test_gap_list_coverage_and_bootstrap_flag);
  RUN_TEST(test_roll_and_monthstart_change_reset);
  return UNITY_END();
}
