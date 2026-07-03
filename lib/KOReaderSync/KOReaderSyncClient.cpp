#include "KOReaderSyncClient.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <MD5Builder.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_mac.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>

#include "KOReaderCredentialStore.h"

int KOReaderSyncClient::lastHttpCode = 0;

namespace {
// Device name reported to the KOSync server
constexpr char DEVICE_NAME[] = "CipherCodex X4";

// KOSync expects a stable per-device id (KOReader sends 32 uppercase hex).
// Derive it as MD5 of the efuse WiFi MAC: unique per device and deterministic
// across boots and firmware updates without persisting anything. Cached in a
// 33-byte static (DRAM) computed once on first use; sync is a cold path.
const char* getDeviceId() {
  static char id[33] = "";
  if (id[0] != '\0') return id;
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  MD5Builder md5;
  md5.begin();
  md5.add(mac, sizeof(mac));
  md5.calculate();
  md5.getChars(id);
  for (char* c = id; *c != '\0'; ++c) {
    *c = static_cast<char>(toupper(static_cast<unsigned char>(*c)));
  }
  return id;
}

// Small TLS buffers to fit in ESP32-C3's limited heap (~46KB free after WiFi).
// KOSync payloads are tiny JSON (<1KB), so 2KB buffers are sufficient.
// Default 16KB buffers cause OOM during TLS handshake.
constexpr int HTTP_BUF_SIZE = 2048;

// Cloudflare tunnels send a 3-cert Google Trust Services chain. During the TLS handshake
// mbedTLS makes many small allocations that collectively consume ~48KB of heap. With only
// ~50KB free after WiFi connects, the session drove min-free-ever down to 2600 bytes before
// failing with MBEDTLS_ERR_X509_ALLOC_FAILED (-0x2880). Check total free heap (not max
// contiguous block) because the failure mode is aggregate exhaustion, not one large alloc.
constexpr uint32_t MIN_HEAP_FOR_TLS = 55000;

// Response buffer for reading HTTP body
struct ResponseBuffer {
  char* data = nullptr;
  int len = 0;
  int capacity = 0;

  ~ResponseBuffer() { free(data); }

  bool ensure(int size) {
    if (size <= capacity) return true;
    char* newData = (char*)realloc(data, size);
    if (!newData) return false;
    data = newData;
    capacity = size;
    return true;
  }
};

// HTTP event handler to collect response body
esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  auto* buf = static_cast<ResponseBuffer*>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf) {
    if (buf->ensure(buf->len + evt->data_len + 1)) {
      memcpy(buf->data + buf->len, evt->data, evt->data_len);
      buf->len += evt->data_len;
      buf->data[buf->len] = '\0';
    } else {
      LOG_ERR("KOSync", "Response buffer allocation failed (%d bytes)", evt->data_len);
    }
  }
  return ESP_OK;
}

// Create configured esp_http_client with small TLS buffers
esp_http_client_handle_t createClient(const char* url, ResponseBuffer* buf,
                                      esp_http_client_method_t method = HTTP_METHOD_GET) {
  esp_http_client_config_t config = {};
  config.url = url;
  config.event_handler = httpEventHandler;
  config.user_data = buf;
  config.method = method;
  config.timeout_ms = 15000;
  config.buffer_size = HTTP_BUF_SIZE;
  config.buffer_size_tx = HTTP_BUF_SIZE;
  config.crt_bundle_attach = esp_crt_bundle_attach;

  // HTTP Basic Auth for Calibre-Web-Automated compatibility
  config.username = KOREADER_STORE.getUsername().c_str();
  config.password = KOREADER_STORE.getPassword().c_str();
  config.auth_type = HTTP_AUTH_TYPE_BASIC;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) return nullptr;

  // KOSync auth headers
  if (esp_http_client_set_header(client, "Accept", "application/vnd.koreader.v1+json") != ESP_OK ||
      esp_http_client_set_header(client, "x-auth-user", KOREADER_STORE.getUsername().c_str()) != ESP_OK ||
      esp_http_client_set_header(client, "x-auth-key", KOREADER_STORE.getMd5Password().c_str()) != ESP_OK) {
    LOG_ERR("KOSync", "Failed to set auth headers");
    esp_http_client_cleanup(client);
    return nullptr;
  }

  return client;
}
}  // namespace

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/users/auth";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("KOSync", "Authenticating: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("KOSync", "Insufficient heap for TLS handshake: %u bytes free (need %u)", freeHeap, MIN_HEAP_FOR_TLS);
    return LOW_MEMORY;
  }

  ResponseBuffer buf;
  esp_http_client_handle_t client = createClient(url.c_str(), &buf);
  if (!client) return NETWORK_ERROR;

  esp_err_t err = esp_http_client_perform(client);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  esp_http_client_cleanup(client);

  LOG_DBG("KOSync", "Auth response: %d (err: %d)", httpCode, err);

  if (err != ESP_OK) return NETWORK_ERROR;
  if (httpCode == 200) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                          KOReaderProgress& outProgress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress/" + documentHash;
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("KOSync", "Getting progress: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("KOSync", "Insufficient heap for TLS handshake: %u bytes free (need %u)", freeHeap, MIN_HEAP_FOR_TLS);
    return LOW_MEMORY;
  }

  ResponseBuffer buf;
  esp_http_client_handle_t client = createClient(url.c_str(), &buf);
  if (!client) return NETWORK_ERROR;

  esp_err_t err = esp_http_client_perform(client);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  esp_http_client_cleanup(client);

  LOG_DBG("KOSync", "Get progress response: %d (err: %d)", httpCode, err);

  if (err != ESP_OK) return NETWORK_ERROR;

  if (httpCode == 200 && buf.data) {
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, buf.data);

    if (error) {
      LOG_ERR("KOSync", "JSON parse failed: %s", error.c_str());
      return JSON_ERROR;
    }

    // The reference kosync server answers 200 with an empty object when nothing is
    // stored; "no progress" is signaled by the absent percentage field, not the status.
    if (doc["percentage"].isNull()) {
      LOG_DBG("KOSync", "No progress stored for document");
      return NOT_FOUND;
    }

    outProgress.document = documentHash;
    outProgress.progress = doc["progress"].as<std::string>();
    outProgress.percentage = doc["percentage"].as<float>();
    outProgress.device = doc["device"].as<std::string>();
    outProgress.deviceId = doc["device_id"].as<std::string>();
    outProgress.timestamp = doc["timestamp"].as<int64_t>();

    LOG_DBG("KOSync", "Got progress: %.2f%% at %s", outProgress.percentage * 100, outProgress.progress.c_str());
    return OK;
  }

  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode == 404) return NOT_FOUND;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  lastHttpCode = 0;
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress";
  const uint32_t freeHeap = ESP.getFreeHeap();
  LOG_DBG("KOSync", "Updating progress: %s (heap: %u)", url.c_str(), (unsigned)freeHeap);
  if (freeHeap < MIN_HEAP_FOR_TLS) {
    LOG_ERR("KOSync", "Insufficient heap for TLS handshake: %u bytes free (need %u)", freeHeap, MIN_HEAP_FOR_TLS);
    return LOW_MEMORY;
  }

  // Build JSON body
  JsonDocument doc;
  doc["document"] = progress.document;
  doc["progress"] = progress.progress;
  // KOReader convention: percentage is a 0..1 fraction truncated to 4 decimals
  // (floor(x*10000)/10000). The CipherCodex Android app relies on this field.
  const double pct = std::max(0.0, std::min(1.0, static_cast<double>(progress.percentage)));
  doc["percentage"] = std::floor(pct * 10000.0) / 10000.0;
  doc["device"] = DEVICE_NAME;
  doc["device_id"] = getDeviceId();

  std::string body;
  serializeJson(doc, body);

  LOG_DBG("KOSync", "Request body: %s", body.c_str());

  ResponseBuffer buf;
  esp_http_client_handle_t client = createClient(url.c_str(), &buf, HTTP_METHOD_PUT);
  if (!client) return NETWORK_ERROR;

  if (esp_http_client_set_header(client, "Content-Type", "application/json") != ESP_OK ||
      esp_http_client_set_post_field(client, body.c_str(), body.length()) != ESP_OK) {
    LOG_ERR("KOSync", "Failed to set request body");
    esp_http_client_cleanup(client);
    return NETWORK_ERROR;
  }

  esp_err_t err = esp_http_client_perform(client);
  const int httpCode = esp_http_client_get_status_code(client);
  lastHttpCode = httpCode;
  esp_http_client_cleanup(client);

  LOG_DBG("KOSync", "Update progress response: %d (err: %d)", httpCode, err);

  if (err != ESP_OK) return NETWORK_ERROR;
  if (httpCode == 200 || httpCode == 202) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

const char* KOReaderSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return "No credentials configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case SERVER_ERROR:
      return "Server error (try again later)";
    case JSON_ERROR:
      return "JSON parse error";
    case NOT_FOUND:
      return "No progress found";
    case LOW_MEMORY:
      return "Not enough memory for sync — please retry";
    default:
      return "Unknown error";
  }
}
