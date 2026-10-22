// Backfill.h - minute-record upload planning over the LittleFS queue (d1 §5.5). Host-portable.
//
// One mechanism serves both the 60 s "minute PATCH" and the back-fill after an outage: records are always uploaded
// from each day's cursor (records already uploaded), oldest day first, and the cursor only moves after a 2xx.
// Keys are deterministic (`hist/<date>/<hhmm>`), so a retry or a crash between the PATCH and the cursor write
// re-sends the same keys: no gaps and no duplicates.
//   * "live" batch: today, at most LIVE_BATCH_MAX pending records -> sent with daily + status (+ alertState)
//   * back-fill batch: up to BACKFILL_MAX_RECS records of the oldest pending day, >= 2 s apart
// h15 aggregates are recomputed from the day file for every quarter that is complete once the batch is uploaded
// (the quarter's minute 14 is present, a later record exists, or the day is over). Day totals for days other than
// the aggregator's running day are recomputed from the file and written per field (nf/nw/epvi are not in the file
// and are left untouched).
#pragma once
#include <stddef.h>
#include <stdint.h>

#include "net/UplinkJson.h"
#include "store/MinuteRec.h"

class IMinuteStore {
 public:
  virtual ~IMinuteStore() {}
  virtual int listDays(int32_t* days, int max) = 0;  // ascending local day numbers
  virtual uint32_t count(int32_t day) = 0;
  virtual uint32_t cursor(int32_t day) = 0;
  virtual bool setCursor(int32_t day, uint32_t n) = 0;
  virtual int read(int32_t day, uint32_t from, MinuteRec* out, int max) = 0;
};

struct Batch {
  int32_t day = 0;
  uint32_t from = 0;
  uint16_t n = 0;
  bool live = false;      // today's small batch (minute PATCH)
  uint32_t pendingTotal = 0;
};

namespace backfill {

// Picks the next batch (oldest day with cursor < count). today = local day number (0 = unknown clock).
// maxRecs must be <= 64 (addRecords' de-duplication table).
bool next(IMinuteStore& st, int32_t today, Batch& b, uint16_t maxRecs = 60, uint16_t liveMax = 2);

// Adds hist/<d>/<hhmm> for the batch records and h15/<d>/<hhmm> for every quarter completed by them.
// recs = records [b.from, b.from + b.n). Returns the number of hist entries added (duplicates within the batch
// collapse to one key: a timed record wins over a provisional estimate).
int addRecords(IMinuteStore& st, const Batch& b, const MinuteRec* recs, int32_t today, upj::Patch& p);

// day totals recomputed from the file, as per-field paths daily/<d>/<f> (no nf/nw/epvi)
bool addDailyFields(IMinuteStore& st, int32_t day, upj::Patch& p);

// Advances the cursor after a successful upload, only if nobody else moved it (returns false if it changed).
bool commit(IMinuteStore& st, const Batch& b);

// oldest pending record's ts (0 if none) - for BACKLOG_STUCK
uint32_t oldestPendingTs(IMinuteStore& st);

}  // namespace backfill
