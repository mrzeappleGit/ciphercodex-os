#include "CipherCodexProgress.h"

#include <gtest/gtest.h>

TEST(CipherCodexProgressTest, ParsesBasicForm) {
  CipherCodexProgress p{};
  ASSERT_TRUE(CipherCodexProgress::parse("ciphercodex:s=12;o=3456", p));
  EXPECT_EQ(p.spineIndex, 12);
  EXPECT_EQ(p.charOffset, 3456);
}

TEST(CipherCodexProgressTest, ParsesZeroValues) {
  CipherCodexProgress p{};
  ASSERT_TRUE(CipherCodexProgress::parse("ciphercodex:s=0;o=0", p));
  EXPECT_EQ(p.spineIndex, 0);
  EXPECT_EQ(p.charOffset, 0);
}

TEST(CipherCodexProgressTest, ParsesLargeOffset) {
  CipherCodexProgress p{};
  ASSERT_TRUE(CipherCodexProgress::parse("ciphercodex:s=3;o=2147483647", p));
  EXPECT_EQ(p.spineIndex, 3);
  EXPECT_EQ(p.charOffset, 2147483647);
}

TEST(CipherCodexProgressTest, RejectsOffsetOverflow) {
  CipherCodexProgress p{};
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3;o=2147483648", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3;o=99999999999999", p));
}

TEST(CipherCodexProgressTest, RejectsKoreaderXPointer) {
  CipherCodexProgress p{};
  EXPECT_FALSE(CipherCodexProgress::parse("/body/DocFragment[20]/body/p[10]/text().0", p));
}

TEST(CipherCodexProgressTest, RejectsMissingParts) {
  CipherCodexProgress p{};
  EXPECT_FALSE(CipherCodexProgress::parse("", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=;o=5", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3;o=", p));
}

TEST(CipherCodexProgressTest, RejectsNonDigits) {
  CipherCodexProgress p{};
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=-1;o=5", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3;o=5.5", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3a;o=5", p));
}

TEST(CipherCodexProgressTest, RejectsSurroundingGarbage) {
  CipherCodexProgress p{};
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3;o=5;x=1", p));
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=3;o=5 ", p));
  EXPECT_FALSE(CipherCodexProgress::parse(" ciphercodex:s=3;o=5", p));
  EXPECT_FALSE(CipherCodexProgress::parse("xciphercodex:s=3;o=5", p));
}

TEST(CipherCodexProgressTest, PrefixIsCaseSensitive) {
  CipherCodexProgress p{};
  EXPECT_FALSE(CipherCodexProgress::parse("CipherCodex:s=3;o=5", p));
}

// A second ";o=" inside the value must not be silently swallowed.
TEST(CipherCodexProgressTest, RejectsDuplicateOffsetSeparator) {
  CipherCodexProgress p{};
  EXPECT_FALSE(CipherCodexProgress::parse("ciphercodex:s=1;o=2;o=3", p));
}
