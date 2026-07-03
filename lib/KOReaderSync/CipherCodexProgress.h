#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

/**
 * Progress string format pushed by the CipherCodex Android companion app:
 *   ciphercodex:s=<spineIndex0Based>;o=<charOffset>
 *
 * The kosync "progress" field is opaque to the server, so this format coexists
 * with KOReader's crengine XPointers. Header-only and dependency-free so the
 * host test suite can cover the parser.
 */
struct CipherCodexProgress {
  int spineIndex = -1;  // 0-based spine item index
  int charOffset = -1;  // 0-based visible-character offset within the spine item

  /** Strict full-string parse, equivalent to ^ciphercodex:s=(\d+);o=(\d+)$. */
  static bool parse(const std::string& progress, CipherCodexProgress& out) {
    static constexpr char kSpinePrefix[] = "ciphercodex:s=";
    static constexpr char kOffsetPrefix[] = ";o=";
    constexpr size_t kSpinePrefixLen = sizeof(kSpinePrefix) - 1;
    constexpr size_t kOffsetPrefixLen = sizeof(kOffsetPrefix) - 1;
    if (progress.compare(0, kSpinePrefixLen, kSpinePrefix) != 0) return false;
    const size_t sep = progress.find(kOffsetPrefix, kSpinePrefixLen);
    if (sep == std::string::npos) return false;
    int spine = 0;
    int offset = 0;
    if (!parseDecimal(progress, kSpinePrefixLen, sep, spine)) return false;
    if (!parseDecimal(progress, sep + kOffsetPrefixLen, progress.size(), offset)) return false;
    out.spineIndex = spine;
    out.charOffset = offset;
    return true;
  }

 private:
  // Digits-only decimal in [start, end); rejects empty ranges and values above INT32_MAX.
  static bool parseDecimal(const std::string& s, size_t start, size_t end, int& out) {
    if (start >= end) return false;
    int64_t val = 0;
    for (size_t i = start; i < end; i++) {
      const char c = s[i];
      if (c < '0' || c > '9') return false;
      val = val * 10 + (c - '0');
      if (val > INT32_MAX) return false;
    }
    out = static_cast<int>(val);
    return true;
  }
};
