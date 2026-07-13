#pragma once

#include <cctype>
#include <cstring>
#include <string>

// Incremental scanner for WebDAV PROPFIND responses: finds <?:href>...</?:href>
// spans in a chunked byte stream (dufs/Nextcloud shape), percent-decodes, drops
// the request path itself, and reports each child's last path segment. No XML
// library on purpose: bounded state, arbitrary chunk splits, ~1KB worst case.
namespace ccxsync {

class CcxHrefScan {
 public:
  using Sink = void (*)(void* ctx, const char* name);

  CcxHrefScan(std::string requestPath, Sink sink, void* ctx)
      : self_(std::move(requestPath)), sink_(sink), ctx_(ctx) {
    while (!self_.empty() && self_.back() == '/') self_.pop_back();
  }

  void feed(const char* data, size_t len) {
    for (size_t i = 0; i < len; i++) step(static_cast<char>(std::tolower(static_cast<unsigned char>(data[i]))), data[i]);
  }

 private:
  static constexpr size_t HREF_MAX = 512;  // hrefs longer than this are dropped

  // Matches "<X:href>" or "<href>" then captures until '<'.
  void step(char lower, char raw) {
    if (!inHref_) {
      tag_ += lower;
      if (tag_.size() > 16) tag_.erase(0, tag_.size() - 16);
      // accept "<href>" with optional one-or-more alpha namespace prefix + ':'
      size_t pos = tag_.rfind("href>");
      if (pos != std::string::npos && pos >= 1) {
        size_t open = tag_.rfind('<', pos);
        if (open != std::string::npos) {
          bool ok = (open + 1 == pos);  // "<href>"
          if (!ok && tag_[pos - 1] == ':') {  // "<d:href>", "<D:href>", "<ns:href>"
            ok = true;
            for (size_t j = open + 1; j + 1 < pos; j++)
              if (!std::isalpha(static_cast<unsigned char>(tag_[j]))) { ok = false; break; }
          }
          if (ok && tag_[open] == '<') { inHref_ = true; href_.clear(); tag_.clear(); return; }
        }
      }
      return;
    }
    if (raw == '<') { emit(); inHref_ = false; tag_.clear(); tag_ += lower; return; }
    if (href_.size() < HREF_MAX) href_ += raw;
  }

  void emit() {
    std::string p = percentDecode(href_);
    while (!p.empty() && p.back() == '/') p.pop_back();
    if (p.empty() || p == self_) return;
    size_t slash = p.rfind('/');
    std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
    if (!name.empty() && sink_) sink_(ctx_, name.c_str());
  }

  static std::string percentDecode(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
      if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
          std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
        auto hex = [](char c) { return (c <= '9') ? c - '0' : (std::tolower(c) - 'a' + 10); };
        out += static_cast<char>(hex(in[i + 1]) * 16 + hex(in[i + 2]));
        i += 2;
      } else {
        out += in[i];
      }
    }
    return out;
  }

  std::string self_, tag_, href_;
  bool inHref_ = false;
  Sink sink_;
  void* ctx_;
};

}  // namespace ccxsync
