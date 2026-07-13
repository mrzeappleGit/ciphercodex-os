#include <gtest/gtest.h>
#include <string>
#include <vector>
#include "CcxSync/CcxHrefScan.h"

using ccxsync::CcxHrefScan;

namespace {
std::vector<std::string> scan(const std::string& xml, const std::string& reqPath, size_t chunk = 7) {
  std::vector<std::string> out;
  CcxHrefScan s(reqPath, [](void* ctx, const char* name) {
    static_cast<std::vector<std::string>*>(ctx)->push_back(name);
  }, &out);
  for (size_t i = 0; i < xml.size(); i += chunk) s.feed(xml.data() + i, std::min(chunk, xml.size() - i));
  return out;
}
const char* kDufs =
    "<?xml version=\"1.0\"?><D:multistatus xmlns:D=\"DAV:\">"
    "<D:response><D:href>/ccx/state/</D:href></D:response>"
    "<D:response><D:href>/ccx/state/aabb01.json</D:href></D:response>"
    "<D:response><D:href>/ccx/state/dead%20beef.json</D:href></D:response>"
    "</D:multistatus>";
}  // namespace

TEST(CcxHrefScan, ExtractsChildrenSkipsSelfDecodesPercent) {
  auto names = scan(kDufs, "/ccx/state/");
  ASSERT_EQ(names.size(), 2u);
  EXPECT_EQ(names[0], "aabb01.json");
  EXPECT_EQ(names[1], "dead beef.json");
}

TEST(CcxHrefScan, SurvivesTagSplitAcrossEveryChunkBoundary) {
  for (size_t chunk = 1; chunk <= 16; chunk++) {
    auto names = scan(kDufs, "/ccx/state/", chunk);
    ASSERT_EQ(names.size(), 2u) << "chunk=" << chunk;
  }
}

TEST(CcxHrefScan, LowercasePrefixAndDirTrailingSlash) {
  auto names = scan(
      "<d:multistatus xmlns:d=\"DAV:\"><d:response><d:href>/ccx/</d:href></d:response>"
      "<d:response><d:href>/ccx/books/</d:href></d:response></d:multistatus>",
      "/ccx/");
  ASSERT_EQ(names.size(), 1u);
  EXPECT_EQ(names[0], "books");
}
