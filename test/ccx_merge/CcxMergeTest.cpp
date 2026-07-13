#include <gtest/gtest.h>
#include "CcxSync/CcxMerge.h"

using namespace ccxsync;

namespace {
MergedBook book(const std::string& d, long long up, int del = 0, const std::string& g = "g") {
  MergedBook b; b.digest = d; b.guid = g; b.title = "t"; b.updatedAt = up; b.deleted = del; return b;
}
MergedBookmark bm(const std::string& g, long long up, int del = 0) {
  MergedBookmark m; m.guid = g; m.bookDigest = "d1"; m.label = "L"; m.updatedAt = up; m.deleted = del; return m;
}
}  // namespace

TEST(CcxMergeWins, LwwAndTombstoneTies) {
  EXPECT_TRUE(wins(20, 0, 10, 0));   // newer remote
  EXPECT_FALSE(wins(10, 0, 20, 0));  // never lower a newer local
  EXPECT_TRUE(wins(10, 1, 10, 0));   // tie: tombstone wins
  EXPECT_FALSE(wins(10, 0, 10, 1));  // tie: live does not beat tombstone
  EXPECT_FALSE(wins(10, 0, 10, 0));  // tie, both live: keep first
}

TEST(CcxMergeAccumulator, BooksLwwByDigestOrderIndependent) {
  Accumulator a, b;
  a.foldBook(book("d1", 10, 0, "guidA")); a.foldBook(book("d1", 20, 0, "guidB"));
  b.foldBook(book("d1", 20, 0, "guidB")); b.foldBook(book("d1", 10, 0, "guidA"));
  ASSERT_EQ(a.books().size(), 1u);
  EXPECT_EQ(a.books()[0].guid, "guidB");
  EXPECT_EQ(b.books()[0].guid, "guidB");
}

TEST(CcxMergeAccumulator, TombstoneBeatsOlderEditAndNoResurrection) {
  Accumulator a;
  a.foldBook(book("d1", 30, 1));
  a.foldBook(book("d1", 10, 0));  // older live copy must not revive
  ASSERT_EQ(a.books().size(), 1u);
  EXPECT_EQ(a.books()[0].deleted, 1);
}

TEST(CcxMergeAccumulator, PdfBooksDropped) {
  MergedBook pdf = book("d2", 10); pdf.format = 0;
  Accumulator a; a.foldBook(pdf);
  EXPECT_TRUE(a.books().empty());
  EXPECT_EQ(a.dropped(), 1u);
}

TEST(CcxMergeAccumulator, LocalDigestFilterDropsForeignChildren) {
  Accumulator a; a.setLocalDigestFilter({"d1"});
  MergedProgress p1; p1.bookDigest = "d1"; p1.updatedAt = 5;
  MergedProgress p2; p2.bookDigest = "dX"; p2.updatedAt = 5;
  a.foldProgress(p1); a.foldProgress(p2);
  ASSERT_EQ(a.progress().size(), 1u);
  EXPECT_EQ(a.progress()[0].bookDigest, "d1");
  EXPECT_EQ(a.dropped(), 1u);
}

TEST(CcxMergeAccumulator, BookmarksLwwByGuid) {
  Accumulator a; a.setLocalDigestFilter({"d1"});
  a.foldBookmark(bm("m1", 10)); a.foldBookmark(bm("m1", 20, 1)); a.foldBookmark(bm("m2", 5));
  ASSERT_EQ(a.bookmarks().size(), 2u);
  for (const auto& m : a.bookmarks())
    if (m.guid == "m1") EXPECT_EQ(m.deleted, 1);
}

TEST(CcxMergeAccumulator, BooksCapDropsAndCounts) {
  Accumulator a;
  for (size_t i = 0; i <= Accumulator::BOOKS_CAP; i++)
    a.foldBook(book("d" + std::to_string(i), 1));
  EXPECT_EQ(a.books().size(), Accumulator::BOOKS_CAP);
  EXPECT_EQ(a.dropped(), 1u);
}
