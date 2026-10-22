// test_rule_parse - alerts.json parsing mirrors the RTDB rules of research/r8-email-alerts.md §3.11 (§4.13).
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>

#include "agg/RuleParse.h"

void setUp() {}
void tearDown() {}

static bool parseOne(const std::string& rule, rules::Def& out, const char* rid = "r01", bool* ok = nullptr) {
  std::string j = std::string("{\"email\":{\"on\":true,\"to\":\"owner@gmail.com,family@gmail.com\"},\"rules\":{\"") + rid +
                  "\":" + rule + "}}";
  rules::Set s;
  char err[64];
  bool r = rules::parse(j.c_str(), j.size(), s, err, sizeof err);
  if (ok) *ok = r;
  int slot = rules::slotFromRid(rid);
  if (slot >= 0) out = s.d[slot];
  return r && slot >= 0 && out.present && !out.invalid;
}

// the nine presets of r8 §6.3 as the PWA stores them
static const char* PRESETS[9] = {
    "{\"name\":\"Daily grid usage > 5 kWh\",\"on\":true,\"metric\":\"egr\",\"scope\":\"day\",\"op\":\"gt\",\"value\":5000,"
    "\"mode\":\"hold\",\"forSec\":0,\"coolMin\":60,\"maxDay\":1,\"sev\":3,\"email\":true,\"ntfy\":false,\"upd\":1792560000000}",
    "{\"name\":\"Load > 2500 W for 5 min\",\"on\":true,\"metric\":\"w\",\"scope\":\"inst\",\"op\":\"gt\",\"value\":2500,"
    "\"mode\":\"hold\",\"forSec\":300,\"clr\":2200,\"clrSec\":120,\"coolMin\":60,\"maxDay\":3,\"sev\":3,\"email\":true,"
    "\"ntfy\":true,\"clrMail\":true,\"upd\":1792560000000}",
    "{\"name\":\"SOC < 30 %\",\"on\":true,\"metric\":\"soc\",\"scope\":\"inst\",\"op\":\"lt\",\"value\":30,\"mode\":\"hold\","
    "\"forSec\":60,\"clr\":35,\"clrSec\":300,\"coolMin\":120,\"maxDay\":3,\"sev\":4,\"email\":true,\"ntfy\":false,"
    "\"clrMail\":true,\"upd\":1792560000000}",
    "{\"name\":\"PV today < 3 kWh by 17:00\",\"on\":true,\"metric\":\"epv\",\"scope\":\"day\",\"op\":\"lt\",\"value\":3000,"
    "\"mode\":\"at\",\"at\":\"1700\",\"minCov\":80,\"maxDay\":1,\"sev\":2,\"email\":true,\"ntfy\":false,\"upd\":1792560000000}",
    "{\"name\":\"Inverter temp > 70 \xC2\xB0" "C\",\"on\":true,\"metric\":\"tmp\",\"scope\":\"inst\",\"op\":\"gt\",\"value\":70,"
    "\"mode\":\"hold\",\"forSec\":120,\"clr\":65,\"clrSec\":300,\"coolMin\":60,\"maxDay\":3,\"sev\":4,\"email\":true,"
    "\"ntfy\":true,\"clrMail\":true,\"upd\":1792560000000}",
    "{\"name\":\"Monthly grid usage > 150 kWh\",\"on\":true,\"metric\":\"egr\",\"scope\":\"month\",\"op\":\"gt\","
    "\"value\":150000,\"mode\":\"hold\",\"maxDay\":1,\"sev\":3,\"email\":true,\"ntfy\":false,\"upd\":1792560000000}",
    "{\"name\":\"Grid outage today > 3 h\",\"on\":true,\"metric\":\"gout\",\"scope\":\"day\",\"op\":\"gt\",\"value\":180,"
    "\"mode\":\"hold\",\"maxDay\":1,\"sev\":3,\"email\":true,\"ntfy\":false,\"upd\":1792560000000}",
    "{\"name\":\"On battery for 2 h\",\"on\":true,\"metric\":\"onbat\",\"scope\":\"inst\",\"op\":\"ge\",\"value\":1,"
    "\"mode\":\"hold\",\"forSec\":7200,\"clrSec\":60,\"coolMin\":120,\"maxDay\":3,\"sev\":3,\"email\":true,\"ntfy\":false,"
    "\"clrMail\":true,\"upd\":1792560000000}",
    "{\"name\":\"Evening peak load > 1500 W\",\"on\":true,\"metric\":\"w\",\"scope\":\"inst\",\"op\":\"gt\",\"value\":1500,"
    "\"mode\":\"hold\",\"forSec\":60,\"clr\":1300,\"clrSec\":120,\"coolMin\":60,\"maxDay\":2,"
    "\"win\":{\"from\":\"1800\",\"to\":\"2300\",\"days\":127},\"sev\":3,\"email\":true,\"ntfy\":false,\"upd\":1792560000000}",
};

void test_presets_parse() {
  for (int i = 0; i < 9; i++) {
    rules::Def d;
    bool ok = parseOne(PRESETS[i], d);
    if (!ok) {
      char m[96];
      snprintf(m, sizeof m, "preset %d: %s", i + 1, d.err);
      TEST_FAIL_MESSAGE(m);
    }
  }
  rules::Def d;
  TEST_ASSERT_TRUE(parseOne(PRESETS[3], d));
  TEST_ASSERT_EQUAL(17 * 60, d.atMin);
  TEST_ASSERT_EQUAL((int)rules::Mode::AT, (int)d.mode);
  TEST_ASSERT_TRUE(parseOne(PRESETS[8], d));
  TEST_ASSERT_TRUE(d.hasWin);
  TEST_ASSERT_EQUAL(18 * 60, d.winFrom);
  TEST_ASSERT_EQUAL(23 * 60, d.winTo);
  TEST_ASSERT_TRUE(parseOne(PRESETS[0], d));
  TEST_ASSERT_EQUAL(0, (int)d.forSec);  // energy default 0
  TEST_ASSERT_EQUAL(0, d.rc);           // all recipients
  TEST_ASSERT_TRUE(parseOne(PRESETS[1], d));
  TEST_ASSERT_TRUE(d.hasClr);
  TEST_ASSERT_EQUAL_FLOAT(2200, d.clr);
  TEST_ASSERT_TRUE(d.ntfy && d.email && d.clrMail);
}

void test_email_policy_and_to_string() {
  const char* j =
      "{\"email\":{\"on\":true,\"to\":\"owner@gmail.com,family@gmail.com\",\"fromName\":\"Solar logger\",\"maxPerDay\":40,"
      "\"monthStart\":5,\"builtin\":{\"minSev\":3,\"clear\":false},\"summary\":{\"on\":true,\"hour\":7},"
      "\"offline\":{\"on\":true,\"min\":30,\"clear\":true}}}";
  rules::Set s;
  char err[64];
  TEST_ASSERT_TRUE(rules::parse(j, strlen(j), s, err, sizeof err));
  TEST_ASSERT_TRUE(s.email.on);
  TEST_ASSERT_EQUAL(5, s.email.monthStart);
  TEST_ASSERT_EQUAL(3, s.email.builtinMinSev);
  TEST_ASSERT_FALSE(s.email.builtinClear);
  TEST_ASSERT_EQUAL(0, s.n);
  // null = nothing configured; malformed JSON = keep the old set
  TEST_ASSERT_TRUE(rules::parse("null", 4, s, err, sizeof err));
  TEST_ASSERT_FALSE(s.email.on);
  TEST_ASSERT_FALSE(rules::parse("{\"email\":", 9, s, err, sizeof err));
  TEST_ASSERT_TRUE(strlen(err) > 0);
}

void test_negative_cases_marked_invalid() {
  rules::Def d;
  // r17 does not exist (the database key pattern allows r01..r16)
  {
    std::string j = std::string("{\"rules\":{\"r17\":") + PRESETS[1] + "}}";
    rules::Set s;
    char err[64];
    TEST_ASSERT_TRUE(rules::parse(j.c_str(), j.size(), s, err, sizeof err));
    TEST_ASSERT_EQUAL(0, s.n);
    TEST_ASSERT_EQUAL(-1, rules::slotFromRid("r17"));
    TEST_ASSERT_EQUAL(-1, rules::slotFromRid("r00"));
    TEST_ASSERT_EQUAL(15, rules::slotFromRid("r16"));
  }
  struct Case { const char* from; const char* to; const char* why; };
  const Case cases[] = {
      {"\"value\":2500", "\"value\":30000", "value out of range"},              // w 30000
      {"\"clr\":2200", "\"clr\":2600", "clr on the wrong side"},
      {"\"email\":true,\"ntfy\":true", "\"email\":false,\"ntfy\":false", "no channel"},
      {"\"metric\":\"w\"", "\"metric\":\"xyz\"", "unknown metric 'xyz'"},
      {"\"scope\":\"inst\"", "\"scope\":\"day\"", "scope does not fit metric"},
      {"\"forSec\":300", "\"forSec\":90000", "bad forSec"},
      {"\"coolMin\":60", "\"coolMin\":2", "bad coolMin"},
      {"\"maxDay\":3", "\"maxDay\":11", "bad maxDay"},
      {"\"sev\":3", "\"sev\":6", "bad sev"},
      {"\"name\":\"Load > 2500 W for 5 min\"", "\"name\":\"Load\\nX\"", "bad name"},
      {",\"upd\":1792560000000", "", "missing upd"},
  };
  for (const Case& c : cases) {
    std::string r = PRESETS[1];
    size_t at = r.find(c.from);
    TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, c.from);
    r.replace(at, strlen(c.from), c.to);
    bool ok = false;
    TEST_ASSERT_FALSE_MESSAGE(parseOne(r, d, "r02", &ok), c.why);
    TEST_ASSERT_TRUE(ok);  // not a parse failure: the slot is marked invalid
    TEST_ASSERT_TRUE(d.invalid);
    TEST_ASSERT_EQUAL_STRING_MESSAGE(c.why, d.err, c.why);
  }
  // egr day lt without mode at
  std::string r = PRESETS[0];
  r.replace(r.find("\"op\":\"gt\""), 9, "\"op\":\"lt\"");
  TEST_ASSERT_FALSE(parseOne(r, d));
  TEST_ASSERT_EQUAL_STRING("day lt/le needs mode at", d.err);
  // month lt
  r = PRESETS[5];
  r.replace(r.find("\"op\":\"gt\""), 9, "\"op\":\"lt\"");
  TEST_ASSERT_FALSE(parseOne(r, d));
  TEST_ASSERT_EQUAL_STRING("month needs gt/ge + hold", d.err);
  // mode at without `at`
  r = PRESETS[3];
  r.replace(r.find("\"at\":\"1700\","), 12, "");
  TEST_ASSERT_FALSE(parseOne(r, d));
  TEST_ASSERT_EQUAL_STRING("mode at needs at", d.err);
  // window with from == to
  r = PRESETS[8];
  r.replace(r.find("\"to\":\"2300\""), 11, "\"to\":\"1800\"");
  TEST_ASSERT_FALSE(parseOne(r, d));
  // repeatMin 10 (allowed: 0 or 30..1440)
  r = PRESETS[1];
  r.replace(r.find("\"maxDay\":3"), 10, "\"maxDay\":3,\"repeatMin\":10");
  TEST_ASSERT_FALSE(parseOne(r, d));
  // a summary error for status.ruleErr
  std::string j = std::string("{\"rules\":{\"r04\":") + "{\"name\":\"x\",\"on\":true,\"metric\":\"xyz\",\"scope\":\"inst\","
                  "\"op\":\"gt\",\"value\":1,\"mode\":\"hold\",\"sev\":3,\"email\":true,\"ntfy\":true,\"upd\":1}}}";
  rules::Set s;
  char err[64];
  TEST_ASSERT_TRUE(rules::parse(j.c_str(), j.size(), s, err, sizeof err));
  TEST_ASSERT_EQUAL(1, s.nInvalid);
  TEST_ASSERT_EQUAL_STRING("r04: unknown metric 'xyz'", s.err);
}

void test_name_length_counts_utf16_units() {
  rules::Def d;
  std::string bangla;
  for (int i = 0; i < 40; i++) bangla += "\xE0\xA6\x95";  // U+0995 BENGALI LETTER KA, 3 bytes in UTF-8
  std::string r = PRESETS[1];
  r.replace(r.find("Load > 2500 W for 5 min"), 23, bangla);
  TEST_ASSERT_TRUE(parseOne(r, d));
  TEST_ASSERT_EQUAL(40, (int)rules::utf16Len(d.name));
  TEST_ASSERT_EQUAL(120, (int)strlen(d.name));
  r = PRESETS[1];
  r.replace(r.find("Load > 2500 W for 5 min"), 23, bangla + "\xE0\xA6\x95");
  TEST_ASSERT_FALSE(parseOne(r, d));  // 41 units
  TEST_ASSERT_EQUAL(2, (int)rules::utf16Len("\xF0\x9F\x98\x80"));  // an emoji is a surrogate pair
  char b[8];
  rules::copyUtf8(b, sizeof b, "ab\xE0\xA6\x95\xE0\xA6\x95");  // 8 bytes into 8: the 2nd letter does not fit
  TEST_ASSERT_EQUAL_STRING("ab\xE0\xA6\x95", b);
}

void test_semantic_hash_ignores_cosmetics() {
  rules::Def a, b;
  TEST_ASSERT_TRUE(parseOne(PRESETS[1], a));
  std::string r = PRESETS[1];
  r.replace(r.find("Load > 2500 W for 5 min"), 23, "Renamed");
  r.replace(r.find("\"sev\":3"), 7, "\"sev\":5");
  r.replace(r.find("\"coolMin\":60"), 12, "\"coolMin\":30");
  TEST_ASSERT_TRUE(parseOne(r, b));
  TEST_ASSERT_EQUAL_UINT32(a.semHash, b.semHash);
  r.replace(r.find("\"forSec\":300"), 12, "\"forSec\":301");
  TEST_ASSERT_TRUE(parseOne(r, b));
  TEST_ASSERT_NOT_EQUAL(a.semHash, b.semHash);
}

// The metric regex of r8 §3.11 (alerts/rules/$rid/metric). If firebase/database.rules.json already carries the merged
// rules, its regex is checked too.
void test_metric_catalogue_matches_rules_regex() {
  char ids[256];
  rules::metricIdList(ids, sizeof ids);
  TEST_ASSERT_EQUAL_STRING("w|pvw|gw|bdw|bw|soc|ld|bv|gv|ov|pvv|bdi|tmp|gf|onbat|nogrid|eld|egr|epv|edis|ech|gout", ids);
  TEST_ASSERT_EQUAL(1, cfg::ALERT_CATALOG_VERSION);
  const char* paths[] = {"../firebase/database.rules.json", "../../firebase/database.rules.json",
                         "firebase/database.rules.json"};
  for (const char* p : paths) {
    FILE* f = fopen(p, "rb");
    if (!f) continue;
    static char buf[65536];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    const char* m = strstr(buf, "matches(/^(w|");
    if (!m) break;  // rules not merged yet
    char want[300];
    snprintf(want, sizeof want, "matches(/^(%s)$/)", ids);
    TEST_ASSERT_TRUE_MESSAGE(strncmp(m, want, strlen(want)) == 0, "metric regex in database.rules.json differs");
    break;
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_presets_parse);
  RUN_TEST(test_email_policy_and_to_string);
  RUN_TEST(test_negative_cases_marked_invalid);
  RUN_TEST(test_name_length_counts_utf16_units);
  RUN_TEST(test_semantic_hash_ignores_cosmetics);
  RUN_TEST(test_metric_catalogue_matches_rules_regex);
  return UNITY_END();
}
