// test_ota - remote firmware update (0.9.2): version codes, the release paths, the SHA256SUMS.txt listing that
// tools/build-release.sh writes, when a request from the app is acted on, and the probation of a new image.
#include <math.h>
#include <string.h>
#include <unity.h>

#include "config.h"
#include "net/OtaPlan.h"
#include "net/UplinkJson.h"

void setUp() {}
void tearDown() {}

void test_version_codes() {
  TEST_ASSERT_EQUAL_UINT32(903, ota::versionCode("0.9.3"));
  TEST_ASSERT_EQUAL_UINT32(10210, ota::versionCode("1.2.10"));
  TEST_ASSERT_EQUAL_UINT32(0, ota::versionCode("0.9"));
  TEST_ASSERT_EQUAL_UINT32(0, ota::versionCode("0.9.3-rc1"));
  TEST_ASSERT_EQUAL_UINT32(0, ota::versionCode("100.0.0"));
  TEST_ASSERT_EQUAL_UINT32(0, ota::versionCode(nullptr));
  char v[16];
  ota::versionStr(903, v, sizeof v);
  TEST_ASSERT_EQUAL_STRING("0.9.3", v);
  ota::versionStr(10210, v, sizeof v);
  TEST_ASSERT_EQUAL_STRING("1.2.10", v);
}

void test_repo_and_paths() {
  TEST_ASSERT_TRUE(ota::repoValid("owner/savx-inverter-wifi-logger"));
  TEST_ASSERT_TRUE(ota::repoValid("A_b.c/d-e"));
  TEST_ASSERT_FALSE(ota::repoValid(""));
  TEST_ASSERT_FALSE(ota::repoValid("owner"));
  TEST_ASSERT_FALSE(ota::repoValid("a/b/c"));
  TEST_ASSERT_FALSE(ota::repoValid("/repo"));
  TEST_ASSERT_FALSE(ota::repoValid("owner/"));
  TEST_ASSERT_FALSE(ota::repoValid("own er/repo"));
  TEST_ASSERT_FALSE(ota::repoValid("../repo"));
  TEST_ASSERT_FALSE(ota::repoValid("owner/repo?x=1"));
  char p[200], f[40];
  ota::imageName(903, f, sizeof f);
  TEST_ASSERT_EQUAL_STRING("savx-logger-0.9.3-ota.bin", f);
  TEST_ASSERT_TRUE(ota::releasePath(p, sizeof p, "owner/repo", 903, f) > 0);
  TEST_ASSERT_EQUAL_STRING("/owner/repo/releases/download/v0.9.3/savx-logger-0.9.3-ota.bin", p);
  TEST_ASSERT_EQUAL_UINT32(0, ota::releasePath(p, 20, "owner/repo", 903, f));  // does not fit
}

// SHA256SUMS.txt of the public release v0.9.1 (tools/build-release.sh, `shasum -a 256`)
static const char* SUMS =
    "5f0fea850d61bc899b282126f6603a0ccf34caa38c50b896cb26c874fa4d9025  savx-logger-0.9.1-esp32s3-n16r8-factory.bin\n"
    "3df13da042035eee3f07a10b9e380dd10bb46607d90c9dd77dc99c4f1a54524e  savx-logger-0.9.1-ota.bin\n";

void test_parse_sums() {
  uint8_t sha[32];
  TEST_ASSERT_TRUE(ota::parseSums(SUMS, strlen(SUMS), "savx-logger-0.9.1-ota.bin", sha));
  TEST_ASSERT_EQUAL_HEX8(0x3d, sha[0]);
  TEST_ASSERT_EQUAL_HEX8(0x4e, sha[31]);
  TEST_ASSERT_TRUE(ota::parseSums(SUMS, strlen(SUMS), "savx-logger-0.9.1-esp32s3-n16r8-factory.bin", sha));
  TEST_ASSERT_EQUAL_HEX8(0x5f, sha[0]);
  TEST_ASSERT_FALSE(ota::parseSums(SUMS, strlen(SUMS), "savx-logger-0.9.2-ota.bin", sha));  // another version
  TEST_ASSERT_FALSE(ota::parseSums(SUMS, strlen(SUMS), "ota.bin", sha));                    // a suffix is no match
  const char* crlf = "3DF13DA042035EEE3F07A10B9E380DD10BB46607D90C9DD77DC99C4F1A54524E *savx-logger-0.9.1-ota.bin\r\n";
  TEST_ASSERT_TRUE(ota::parseSums(crlf, strlen(crlf), "savx-logger-0.9.1-ota.bin", sha));  // upper case, binary mark
  TEST_ASSERT_EQUAL_HEX8(0x3d, sha[0]);
  const char* bad = "3df13da042035eee3f07a10b9e380dd10bb46607d90c9dd77dc99c4f1a54524g  savx-logger-0.9.1-ota.bin\n";
  TEST_ASSERT_FALSE(ota::parseSums(bad, strlen(bad), "savx-logger-0.9.1-ota.bin", sha));
}

// 0.9.7: 0.9.6's boot check showed "GitHub ok, latest ?": the factory image's line comes first in the listing and
// the version was read across two lines.
void test_version_in_sums() {
  char v[16] = "";
  TEST_ASSERT_TRUE(ota::versionInSums(SUMS, strlen(SUMS), v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("0.9.1", v);
  const char* crlf = "3df13da042035eee3f07a10b9e380dd10bb46607d90c9dd77dc99c4f1a54524e  savx-logger-1.2.10-ota.bin\r\n";
  TEST_ASSERT_TRUE(ota::versionInSums(crlf, strlen(crlf), v, sizeof v));
  TEST_ASSERT_EQUAL_STRING("1.2.10", v);
  const char* none = "5f0f  savx-logger-0.9.1-esp32s3-n16r8-factory.bin\n";
  TEST_ASSERT_FALSE(ota::versionInSums(none, strlen(none), v, sizeof v));
}

void test_decide_once_and_only_newer() {
  using ota::Go;
  TEST_ASSERT_EQUAL_INT((int)Go::START, (int)ota::decide(903, 5, 902, 0, true));
  TEST_ASSERT_EQUAL_INT((int)Go::NONE, (int)ota::decide(903, 5, 902, 5, true));  // already acted on: no retry loop
  TEST_ASSERT_EQUAL_INT((int)Go::START, (int)ota::decide(903, 6, 902, 5, true));  // the owner asked again
  TEST_ASSERT_EQUAL_INT((int)Go::RESTART, (int)ota::decide(903, 6, 903, 5, true));  // 1.0.0: same version = restart
  TEST_ASSERT_EQUAL_INT((int)Go::NONE, (int)ota::decide(903, 6, 903, 6, true));     // ...once
  TEST_ASSERT_EQUAL_INT((int)Go::NOT_NEWER, (int)ota::decide(901, 6, 903, 5, true));  // no downgrades
  TEST_ASSERT_EQUAL_INT((int)Go::NO_REPO, (int)ota::decide(903, 6, 902, 5, false));
  TEST_ASSERT_EQUAL_INT((int)Go::NONE, (int)ota::decide(903, 0, 902, 5, true));
  TEST_ASSERT_EQUAL_INT((int)Go::NONE, (int)ota::decide(0, 6, 902, 5, true));
}

// 0.9.4: 0.9.2 refused the owner's first remote update with "low memory (31732)": it asked for a 45 KB block, but
// the logger's largest free block is ~32 KB, the fragmentation its other TLS sessions run in.
void test_heap_gate_accepts_the_owners_logger() {
  TEST_ASSERT_TRUE(ota::heapOk(31732, 95000));
  TEST_ASSERT_FALSE(ota::heapOk(15000, 95000));  // no room for the 16 KB TLS record buffer
  TEST_ASSERT_FALSE(ota::heapOk(31732, 30000));  // not enough in total for a session
}

// Owner's version rule (2026-10-24): A.B.C, B and C 0-9, rolling over; this build must follow it.
void test_version_rule() {
  TEST_ASSERT_TRUE_MESSAGE(ota::versionRuleOk(FW_VERSION), FW_VERSION);
  TEST_ASSERT_TRUE(ota::versionRuleOk("1.0.0"));
  TEST_ASSERT_TRUE(ota::versionRuleOk("0.9.9"));
  TEST_ASSERT_TRUE(ota::versionRuleOk("12.9.9"));
  TEST_ASSERT_FALSE(ota::versionRuleOk("0.9.10"));
  TEST_ASSERT_FALSE(ota::versionRuleOk("1.10.0"));
  TEST_ASSERT_FALSE(ota::versionRuleOk("1.0"));
  TEST_ASSERT_FALSE(ota::versionRuleOk("100.0.0"));
  // the order holds across each rollover, also for the 0.9.x loggers that read cfg.ota
  TEST_ASSERT_TRUE(ota::versionCode("1.0.0") > ota::versionCode("0.9.9"));
  TEST_ASSERT_TRUE(ota::versionCode("1.1.0") > ota::versionCode("1.0.9"));
  TEST_ASSERT_TRUE(ota::versionCode("2.0.0") > ota::versionCode("1.9.9"));
}

void test_probation() {
  using ota::Probation;
  const uint32_t MIN = 120000, MAX = 600000;
  TEST_ASSERT_EQUAL_INT((int)Probation::WAIT, (int)ota::probation(60000, true, true, true, MIN, MAX));
  TEST_ASSERT_EQUAL_INT((int)Probation::CONFIRM, (int)ota::probation(130000, true, true, true, MIN, MAX));
  TEST_ASSERT_EQUAL_INT((int)Probation::WAIT, (int)ota::probation(300000, false, true, true, MIN, MAX));
  TEST_ASSERT_EQUAL_INT((int)Probation::ROLLBACK, (int)ota::probation(600000, false, true, true, MIN, MAX));
  TEST_ASSERT_EQUAL_INT((int)Probation::CONFIRM, (int)ota::probation(600000, true, true, true, MIN, MAX));
  // 1.0.0: an image that writes to the cloud but cannot read cfg (update requests) or reach GitHub is not kept
  TEST_ASSERT_EQUAL_INT((int)Probation::WAIT, (int)ota::probation(300000, true, false, true, MIN, MAX));
  TEST_ASSERT_EQUAL_INT((int)Probation::ROLLBACK, (int)ota::probation(600000, true, false, true, MIN, MAX));
  TEST_ASSERT_EQUAL_INT((int)Probation::WAIT, (int)ota::probation(300000, true, true, false, MIN, MAX));
  TEST_ASSERT_EQUAL_INT((int)Probation::ROLLBACK, (int)ota::probation(600000, true, true, false, MIN, MAX));
}

void test_cfg_ota_fields() {
  upj::CfgDoc c;
  const char* j = "{\"ota\":903,\"otaAt\":1792790000123}";
  TEST_ASSERT_TRUE(upj::parseCfg(j, strlen(j), c));
  TEST_ASSERT_EQUAL_UINT32(903, c.c.ota);
  TEST_ASSERT_EQUAL_UINT32((uint32_t)fmod(1792790000123.0, 4294967296.0), c.c.otaAt);
  AlertCfg a = c.c, b = c.c;
  TEST_ASSERT_TRUE(upj::cfgEq(a, b));
  b.otaAt++;
  TEST_ASSERT_FALSE(upj::cfgEq(a, b));  // a new request is a change
  TEST_ASSERT_TRUE(upj::parseCfg("{\"ota\":-1}", 10, c));
  TEST_ASSERT_EQUAL_UINT32(0, c.c.ota);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_version_codes);
  RUN_TEST(test_repo_and_paths);
  RUN_TEST(test_parse_sums);
  RUN_TEST(test_version_in_sums);
  RUN_TEST(test_decide_once_and_only_newer);
  RUN_TEST(test_heap_gate_accepts_the_owners_logger);
  RUN_TEST(test_version_rule);
  RUN_TEST(test_probation);
  RUN_TEST(test_cfg_ota_fields);
  return UNITY_END();
}
