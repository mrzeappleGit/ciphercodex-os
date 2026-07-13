#include <gtest/gtest.h>
#include <string>
#include "CcxSync/CcxMerge.h"
#include "CcxSync/CcxSnapshotParse.h"

namespace {
// rM2-shaped snapshot: ink arrays present (one points_b64 > 512 bytes to prove
// token-overflow tolerance), one pdf book, camelCase contract fields.
std::string rm2Snapshot() {
  std::string bigBlob(700, 'A');
  return std::string("{\"deviceId\":\"aabb01\",\"generatedAt\":1,")
    + "\"books\":[{\"digest\":\"d1\",\"guid\":\"g1\",\"title\":\"Dune\",\"author\":\"H\",\"format\":1,"
      "\"addedAt\":1,\"lastOpenedAt\":2,\"deleted\":0,\"updatedAt\":10},"
      "{\"digest\":\"d2\",\"guid\":\"g2\",\"title\":\"Scan\",\"format\":0,\"deleted\":0,\"updatedAt\":11}],"
    + "\"progress\":[{\"bookDigest\":\"d1\",\"spineIndex\":3,\"charOffset\":120,\"percentage\":0.25,"
      "\"deleted\":0,\"updatedAt\":11}],"
    + "\"bookmarks\":[{\"guid\":\"m1\",\"bookDigest\":\"d1\",\"spineIndex\":1,\"charOffset\":5,"
      "\"percentage\":0.1,\"label\":\"start\",\"createdAt\":3,\"deleted\":0,\"updatedAt\":12}],"
    + "\"highlights\":[{\"guid\":\"h1\",\"bookDigest\":\"d1\",\"text\":\"x\",\"deleted\":0,\"updatedAt\":13}],"
    + "\"notebooks\":[{\"guid\":\"n1\",\"title\":\"ink\",\"deleted\":0,\"updatedAt\":16}],"
    + "\"strokes\":[{\"guid\":\"s1\",\"pageGuid\":\"p1\",\"tool\":0,\"baseWidth\":2.0,"
      "\"points_b64\":\"" + bigBlob + "\",\"deleted\":0,\"updatedAt\":18}]}";
}
}  // namespace

TEST(CcxSnapshotParse, FoldsWantedRowsSkipsInkAndPdf) {
  ccxsync::Accumulator acc;
  acc.setLocalDigestFilter({"d1"});
  CcxSnapshotParse p(acc);
  std::string s = rm2Snapshot();
  for (size_t i = 0; i < s.size(); i += 13) p.feed(s.data() + i, std::min<size_t>(13, s.size() - i));
  EXPECT_FALSE(p.failed());
  ASSERT_EQ(acc.books().size(), 1u);          // pdf dropped
  EXPECT_EQ(acc.books()[0].digest, "d1");
  EXPECT_EQ(acc.books()[0].title, "Dune");
  ASSERT_EQ(acc.progress().size(), 1u);
  EXPECT_EQ(acc.progress()[0].spineIndex, 3);
  EXPECT_FLOAT_EQ(acc.progress()[0].percentage, 0.25f);
  ASSERT_EQ(acc.bookmarks().size(), 1u);
  EXPECT_EQ(acc.bookmarks()[0].label, "start");
}

TEST(CcxSnapshotParse, MissingArraysAndUnknownFieldsAreFine) {
  ccxsync::Accumulator acc;
  CcxSnapshotParse p(acc);
  const char* s = "{\"deviceId\":\"x\",\"future\":{\"nested\":[1,2]}}";
  p.feed(s, strlen(s));
  EXPECT_FALSE(p.failed());
  EXPECT_TRUE(acc.books().empty());
}
