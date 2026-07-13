#include "CcxWebDav.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <base64.h>
#include <esp_crt_bundle.h>

#include "CcxHrefScan.h"

namespace {
constexpr int HTTP_TIMEOUT_MS = 30000;
constexpr size_t READ_CHUNK = 2048;
constexpr size_t MIN_HEAP_FOR_TLS = 55000;  // mbedTLS handshake can eat ~48KB (KOReaderSyncClient lesson)
constexpr char PROPFIND_BODY[] =
    "<?xml version=\"1.0\"?><d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/></d:prop></d:propfind>";

std::string base64Encode(const std::string& in) {
  // Reuses the Arduino base64 lib already linked for HttpDownloader's Basic
  // auth header -- no need for a second (mbedtls) implementation.
  return std::string(base64::encode(in.c_str()).c_str());
}

// Strips "scheme://host[:port]" off a full URL, leaving the root-relative
// path dufs echoes back in PROPFIND <href> elements (used as CcxHrefScan's
// "self" path so the request directory doesn't show up as its own child).
std::string urlPath(const std::string& url) {
  size_t schemeEnd = url.find("://");
  if (schemeEnd == std::string::npos) return url;
  size_t pathStart = url.find('/', schemeEnd + 3);
  return pathStart == std::string::npos ? "/" : url.substr(pathStart);
}

// request()'s generic response sink for getToFile(): appends each chunk to
// the open temp file.
bool fileWriteSink(void* ctx, const char* data, size_t len) {
  auto* f = static_cast<HalFile*>(ctx);
  return f->write(reinterpret_cast<const uint8_t*>(data), len) == len;
}

// request()'s generic response sink for list(): feeds raw PROPFIND bytes
// into the href scanner.
bool hrefFeedSink(void* ctx, const char* data, size_t len) {
  static_cast<ccxsync::CcxHrefScan*>(ctx)->feed(data, len);
  return true;
}

struct ListCtx {
  std::vector<std::string>* out;
  size_t cap;
  bool capped = false;
};

// CcxHrefScan::Sink: receives one decoded child name at a time.
void listNameSink(void* ctx, const char* name) {
  auto* lc = static_cast<ListCtx*>(ctx);
  if (lc->out->size() >= lc->cap) {
    if (!lc->capped) {
      LOG_INF("CCXDAV", "list: capped at %zu entries", lc->cap);
      lc->capped = true;
    }
    return;
  }
  lc->out->emplace_back(name);
}
}  // namespace

CcxWebDav::CcxWebDav(std::string baseUrl, std::string user, std::string pass) {
  base_ = std::move(baseUrl);
  if (base_.empty() || base_.back() != '/') base_ += '/';
  auth_ = "Basic " + base64Encode(user + ":" + pass);
}

bool CcxWebDav::request(esp_http_client_method_t method, const std::string& relPath, const RequestOpts& opts) {
  lastStatus_ = 0;  // a transport failure must not leave a stale status from the previous call
  const std::string url = base_ + relPath;

  if (url.rfind("https://", 0) == 0 && ESP.getFreeHeap() < MIN_HEAP_FOR_TLS) {
    LOG_ERR("CCXDAV", "heap too low for TLS: %u free (need %u)", (unsigned)ESP.getFreeHeap(), (unsigned)MIN_HEAP_FOR_TLS);
    return false;
  }

  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.timeout_ms = HTTP_TIMEOUT_MS;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 1024;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    LOG_ERR("CCXDAV", "client init failed");
    return false;
  }

  esp_http_client_set_method(client, method);
  esp_http_client_set_header(client, "Authorization", auth_.c_str());
  if (opts.depth) esp_http_client_set_header(client, "Depth", opts.depth);
  if (opts.destination) {
    esp_http_client_set_header(client, "Destination", opts.destination);
    esp_http_client_set_header(client, "Overwrite", "T");
  }
  if (opts.contentType) esp_http_client_set_header(client, "Content-Type", opts.contentType);

  const size_t writeLen = opts.uploadFile ? opts.uploadSize : opts.bodyLen;
  esp_err_t err = esp_http_client_open(client, static_cast<int>(writeLen));
  if (err != ESP_OK) {
    LOG_ERR("CCXDAV", "open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return false;
  }

  if (opts.uploadFile) {
    auto buf = makeUniqueNoThrow<char[]>(READ_CHUNK);
    if (!buf) {
      LOG_ERR("CCXDAV", "OOM: %u byte upload buffer", (unsigned)READ_CHUNK);
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
    size_t remaining = opts.uploadSize;
    while (remaining > 0) {
      const size_t chunk = remaining < READ_CHUNK ? remaining : READ_CHUNK;
      const int n = opts.uploadFile->read(buf.get(), chunk);
      if (n <= 0) {
        LOG_ERR("CCXDAV", "putFile: short read, %u bytes remaining", (unsigned)remaining);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }
      const int written = esp_http_client_write(client, buf.get(), n);
      if (written != n) {
        LOG_ERR("CCXDAV", "putFile: write failed");
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
      }
      remaining -= static_cast<size_t>(n);
    }
  } else if (opts.body && opts.bodyLen > 0) {
    const int written = esp_http_client_write(client, opts.body, static_cast<int>(opts.bodyLen));
    if (written < 0 || static_cast<size_t>(written) != opts.bodyLen) {
      LOG_ERR("CCXDAV", "write body failed");
      esp_http_client_close(client);
      esp_http_client_cleanup(client);
      return false;
    }
  }

  esp_http_client_fetch_headers(client);
  lastStatus_ = esp_http_client_get_status_code(client);

  auto buf = makeUniqueNoThrow<char[]>(READ_CHUNK);
  if (!buf) {
    LOG_ERR("CCXDAV", "OOM: %u byte read buffer", (unsigned)READ_CHUNK);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return false;
  }
  bool sinkOk = true;
  while (true) {
    const int read = esp_http_client_read(client, buf.get(), READ_CHUNK);
    if (read < 0) {
      LOG_ERR("CCXDAV", "read error");
      sinkOk = false;
      break;
    }
    if (read == 0) break;
    if (opts.sink && !opts.sink(opts.sinkCtx, buf.get(), static_cast<size_t>(read))) {
      sinkOk = false;  // sink asked us to abort
      break;
    }
  }

  esp_http_client_close(client);
  esp_http_client_cleanup(client);

  if (!sinkOk) return false;
  return lastStatus_ >= 200 && lastStatus_ < 300;
}

bool CcxWebDav::test() {
  RequestOpts opts;
  opts.depth = "0";
  opts.contentType = "application/xml";
  opts.body = PROPFIND_BODY;
  opts.bodyLen = sizeof(PROPFIND_BODY) - 1;
  return request(HTTP_METHOD_PROPFIND, "", opts);
}

bool CcxWebDav::list(const std::string& relDir, std::vector<std::string>& outNames, size_t cap) {
  outNames.clear();

  std::string dir = relDir;
  while (!dir.empty() && dir.front() == '/') dir.erase(0, 1);
  if (!dir.empty() && dir.back() != '/') dir += '/';

  ListCtx ctx{&outNames, cap};
  ccxsync::CcxHrefScan scan(urlPath(base_) + dir, &listNameSink, &ctx);

  RequestOpts opts;
  opts.depth = "1";
  opts.contentType = "application/xml";
  opts.body = PROPFIND_BODY;
  opts.bodyLen = sizeof(PROPFIND_BODY) - 1;
  opts.sink = &hrefFeedSink;
  opts.sinkCtx = &scan;
  return request(HTTP_METHOD_PROPFIND, dir, opts);
}

bool CcxWebDav::getToFile(const std::string& relPath, const std::string& destPath) {
  const std::string tmpPath = destPath + ".tmp";
  bool ok;
  {
    HalFile tmp;
    if (!Storage.openFileForWrite("CCXDAV", tmpPath, tmp)) {
      LOG_ERR("CCXDAV", "getToFile: could not open temp file %s", tmpPath.c_str());
      return false;
    }
    RequestOpts opts;
    opts.sink = &fileWriteSink;
    opts.sinkCtx = &tmp;
    ok = request(HTTP_METHOD_GET, relPath, opts);
  }  // tmp closes here (DESTRUCTOR_CLOSES_FILE) before the rename below

  if (!ok) {
    Storage.remove(tmpPath.c_str());
    return false;
  }
  Storage.remove(destPath.c_str());
  if (!Storage.rename(tmpPath.c_str(), destPath.c_str())) {
    LOG_ERR("CCXDAV", "getToFile: rename failed %s -> %s", tmpPath.c_str(), destPath.c_str());
    return false;
  }
  return true;
}

bool CcxWebDav::getStreaming(const std::string& relPath, DataFn onData, void* ctx) {
  RequestOpts opts;
  opts.sink = onData;
  opts.sinkCtx = ctx;
  return request(HTTP_METHOD_GET, relPath, opts);
}

bool CcxWebDav::putFile(const std::string& relPath, const std::string& srcPath, const char* contentType) {
  HalFile src;
  if (!Storage.openFileForRead("CCXDAV", srcPath, src)) {
    LOG_ERR("CCXDAV", "putFile: could not open %s", srcPath.c_str());
    return false;
  }
  RequestOpts opts;
  opts.contentType = contentType;
  opts.uploadFile = &src;
  opts.uploadSize = src.fileSize();
  return request(HTTP_METHOD_PUT, relPath, opts);
}

bool CcxWebDav::mkcol(const std::string& relDir) {
  RequestOpts opts;
  if (request(HTTP_METHOD_MKCOL, relDir, opts)) return true;
  return lastStatus_ == 405;  // collection already exists -- also fine
}

bool CcxWebDav::move(const std::string& fromRel, const std::string& toRel) {
  const std::string dest = base_ + toRel;
  RequestOpts opts;
  opts.destination = dest.c_str();
  return request(HTTP_METHOD_MOVE, fromRel, opts);
}
