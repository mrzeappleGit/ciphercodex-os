#pragma once

#include <HalStorage.h>
#include <esp_http_client.h>

#include <string>
#include <vector>

// Minimal WebDAV client over esp_http_client: PROPFIND-based listing (fed
// through CcxHrefScan), streaming GET/PUT, MKCOL, MOVE. Talks to the dufs
// endpoint used for CipherCodex sync -- no retries, no redirect-following
// (the endpoint doesn't redirect; that's OTA-CDN territory, see HttpDownloader).
class CcxWebDav {
 public:
  // baseUrl is normalized to end with '/'. auth_ (Basic <base64 user:pass>)
  // is built once here and reused for every request.
  CcxWebDav(std::string baseUrl, std::string user, std::string pass);

  // PROPFIND base_, Depth 0. Just checks the server/credentials/path work.
  bool test();

  // PROPFIND relDir, Depth 1, streamed through CcxHrefScan. Appends up to
  // `cap` child names to outNames (clears it first). False on HTTP error.
  bool list(const std::string& relDir, std::vector<std::string>& outNames, size_t cap);

  // GET relPath, writing the body to destPath via a ".tmp" + remove+rename
  // swap (ProgressFile::writeAtomic pattern) so a failed/aborted transfer
  // never leaves destPath half-written.
  bool getToFile(const std::string& relPath, const std::string& destPath);

  // Returns false from onData to abort the transfer early.
  using DataFn = bool (*)(void* ctx, const char* data, size_t len);
  bool getStreaming(const std::string& relPath, DataFn onData, void* ctx);

  // PUT relPath with the contents of srcPath (read from SD via HalFile).
  bool putFile(const std::string& relPath, const std::string& srcPath, const char* contentType);

  // MKCOL relDir. 405 (already exists) counts as success, same as 2xx.
  bool mkcol(const std::string& relDir);

  // MOVE fromRel -> base_ + toRel, Overwrite: T.
  bool move(const std::string& fromRel, const std::string& toRel);

  int lastStatus() const { return lastStatus_; }

 private:
  // Every verb funnels through this one worker; the options it doesn't need
  // stay at their defaults (nullptr/0).
  struct RequestOpts {
    const char* depth = nullptr;        // sets "Depth" header (PROPFIND)
    const char* destination = nullptr;  // sets "Destination" + "Overwrite: T" (MOVE)
    const char* contentType = nullptr;  // sets "Content-Type" header
    const char* body = nullptr;         // fixed in-memory request body (PROPFIND)
    size_t bodyLen = 0;
    HalFile* uploadFile = nullptr;  // stream this file as the request body (PUT); takes priority over body
    size_t uploadSize = 0;
    DataFn sink = nullptr;   // response body sink; nullptr discards the body
    void* sinkCtx = nullptr;
  };

  bool request(esp_http_client_method_t method, const std::string& relPath, const RequestOpts& opts);

  std::string base_, auth_;  // auth_ = "Basic " + base64(user:pass), built once
  int lastStatus_ = 0;
};
