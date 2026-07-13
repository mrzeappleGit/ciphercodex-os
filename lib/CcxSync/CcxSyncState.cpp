#include "CcxSyncState.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstdio>

namespace {
constexpr char GLOBAL_FILE[] = "/.crosspoint/ccxsync.json";
constexpr size_t BM_GUIDS_CAP = 32;

std::string bookFilePath(const std::string& cacheDir) { return cacheDir + "/ccxsync.json"; }

// Writes `doc` to `path` via a ".tmp" + remove+rename swap (ProgressFile
// pattern / CcxWebDav::getToFile) so a crash mid-write never leaves the JSON
// file half-written.
bool writeJsonAtomic(const std::string& path, JsonDocument& doc) {
  const std::string tmpPath = path + ".tmp";
  {
    HalFile f;
    if (!Storage.openFileForWrite("CCXST", tmpPath, f)) {
      LOG_ERR("CCXST", "could not open temp file %s", tmpPath.c_str());
      return false;
    }
    String json;
    serializeJson(doc, json);
    const size_t written = f.write(reinterpret_cast<const uint8_t*>(json.c_str()), json.length());
    if (written != json.length()) {
      LOG_ERR("CCXST", "short write to %s", tmpPath.c_str());
      return false;
    }
    f.flush();
  }  // f (the temp file) closes here before the rename below
  Storage.remove(path.c_str());
  if (!Storage.rename(tmpPath.c_str(), path.c_str())) {
    LOG_ERR("CCXST", "rename failed %s -> %s", tmpPath.c_str(), path.c_str());
    return false;
  }
  return true;
}
}  // namespace

CcxSyncState CcxSyncState::instance;

std::string CcxSyncState::newGuid() {
  char hex[33];
  for (int i = 0; i < 4; i++) {
    snprintf(hex + i * 8, 9, "%08x", static_cast<unsigned>(esp_random()));
  }
  return std::string(hex, 32);
}

bool CcxSyncState::loadGlobal() {
  if (!Storage.exists(GLOBAL_FILE)) return false;
  String json = Storage.readFile(GLOBAL_FILE);
  if (json.isEmpty()) return false;

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CCXST", "global JSON parse error: %s", error.c_str());
    return false;
  }

  url = doc["url"] | std::string("");
  user = doc["user"] | std::string("");
  pass = doc["pass"] | std::string("");
  deviceId = doc["deviceId"] | std::string("");
  lastSyncAt = doc["lastSyncAt"] | 0LL;

  tombstones.clear();
  for (JsonObjectConst o : doc["tombstones"].as<JsonArrayConst>()) {
    Tombstone t;
    t.guid = o["guid"] | std::string("");
    t.digest = o["digest"] | std::string("");
    t.at = o["at"] | 0LL;
    tombstones.push_back(std::move(t));
  }

  return true;
}

bool CcxSyncState::saveGlobal() {
  Storage.mkdir("/.crosspoint");
  if (deviceId.empty()) deviceId = newGuid();

  if (tombstones.size() > TOMBSTONES_CAP) {
    tombstones.erase(tombstones.begin(), tombstones.end() - TOMBSTONES_CAP);  // drop oldest
  }

  JsonDocument doc;
  doc["url"] = url;
  doc["user"] = user;
  doc["pass"] = pass;
  doc["deviceId"] = deviceId;
  doc["lastSyncAt"] = lastSyncAt;
  JsonArray arr = doc["tombstones"].to<JsonArray>();
  for (const auto& t : tombstones) {
    JsonObject o = arr.add<JsonObject>();
    o["guid"] = t.guid;
    o["digest"] = t.digest;
    o["at"] = t.at;
  }

  return writeJsonAtomic(GLOBAL_FILE, doc);
}

bool CcxSyncState::loadBook(const std::string& cacheDir, CcxBookState& out) {
  const std::string path = bookFilePath(cacheDir);
  if (!Storage.exists(path.c_str())) return false;
  String json = Storage.readFile(path.c_str());
  if (json.isEmpty()) return false;

  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("CCXST", "book JSON parse error (%s): %s", cacheDir.c_str(), error.c_str());
    return false;
  }

  out.path = doc["path"] | std::string("");
  out.fileSize = doc["size"] | 0LL;
  out.fileMtime = doc["mtime"] | 0LL;
  out.digest = doc["digest"] | std::string("");
  out.guid = doc["guid"] | std::string("");
  out.firstSeenAt = doc["firstSeenAt"] | 0LL;
  out.localStamp = doc["localStamp"] | 0LL;
  out.pendSpine = doc["pendSpine"] | -1;
  out.pendPct = doc["pendPct"] | 0.0f;
  out.pendAt = doc["pendAt"] | 0LL;
  out.dirtyProgress = doc["dirtyP"] | false;
  out.dirtyBookmarks = doc["dirtyB"] | false;

  out.bmGuids.clear();
  for (JsonVariantConst v : doc["bmGuids"].as<JsonArrayConst>()) {
    out.bmGuids.push_back(v.as<std::string>());
  }

  return true;
}

bool CcxSyncState::saveBook(const std::string& cacheDir, const CcxBookState& s) {
  JsonDocument doc;
  doc["path"] = s.path;
  doc["size"] = s.fileSize;
  doc["mtime"] = s.fileMtime;
  doc["digest"] = s.digest;
  doc["guid"] = s.guid;
  doc["firstSeenAt"] = s.firstSeenAt;
  doc["localStamp"] = s.localStamp;
  doc["pendSpine"] = s.pendSpine;
  doc["pendPct"] = s.pendPct;
  doc["pendAt"] = s.pendAt;
  doc["dirtyP"] = s.dirtyProgress;
  doc["dirtyB"] = s.dirtyBookmarks;

  JsonArray arr = doc["bmGuids"].to<JsonArray>();
  const size_t start = s.bmGuids.size() > BM_GUIDS_CAP ? s.bmGuids.size() - BM_GUIDS_CAP : 0;
  for (size_t i = start; i < s.bmGuids.size(); i++) arr.add(s.bmGuids[i]);

  Storage.mkdir(cacheDir.c_str());  // usually already exists (progress.bin lives here); cheap no-op if so
  return writeJsonAtomic(bookFilePath(cacheDir), doc);
}

void CcxSyncState::markProgressDirty(const std::string& cacheDir) {
  CcxBookState s;
  loadBook(cacheDir, s);  // false = no state yet; s stays default (path/digest filled in at sync time)
  s.dirtyProgress = true;
  saveBook(cacheDir, s);
}

void CcxSyncState::markBookmarksDirty(const std::string& cacheDir) {
  CcxBookState s;
  loadBook(cacheDir, s);
  s.dirtyBookmarks = true;
  saveBook(cacheDir, s);
}
