#pragma once

#include <FsHelpers.h>
#include <HalStorage.h>

#include <functional>
#include <string>

#include "components/themes/BaseTheme.h"

// Cheap per-book reading indicator for library lists. A single progress.bin
// existence check (one SD stat) distinguishes a never-opened book from one with
// saved progress, matching the design comp's leading reading-state glyph.
//
// The cache path mirrors the readers' scheme exactly:
//   /.crosspoint/<epub|txt|xtc>_<hash(fullPath)>/progress.bin
// (see Epub.h ctor and EpubReaderActivity). `fullPath` MUST be the same string
// the reader opens with (basepath + "/" + name) or the hash won't match.
//
// A true "finished" (checkmark) state is intentionally not distinguished: it
// would require the book's total spine count, i.e. a per-book metadata load on
// every render, which the device's RAM budget does not justify here.
namespace ReadingState {

inline UIIcon glyphForBook(const std::string& fullPath) {
  const char* prefix = nullptr;
  if (FsHelpers::hasEpubExtension(fullPath)) {
    prefix = "epub_";
  } else if (FsHelpers::hasXtcExtension(fullPath)) {
    prefix = "xtc_";
  } else if (FsHelpers::hasTxtExtension(fullPath) || FsHelpers::hasMarkdownExtension(fullPath)) {
    // .md opens in the Txt reader, which caches under the txt_ prefix.
    prefix = "txt_";
  }
  if (!prefix) {
    return UIIcon::None;
  }
  const std::string progressPath =
      "/.crosspoint/" + std::string(prefix) + std::to_string(std::hash<std::string>{}(fullPath)) + "/progress.bin";
  return Storage.exists(progressPath.c_str()) ? UIIcon::BookReading : UIIcon::BookNew;
}

}  // namespace ReadingState
