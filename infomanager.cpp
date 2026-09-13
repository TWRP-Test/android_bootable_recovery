/*
	Copyright 2012 bigbiff/Dees_Troy TeamWin
	This file is part of TWRP/TeamWin Recovery Project.

	TWRP is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	TWRP is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with TWRP.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <sys/stat.h>

#include <charconv>
#include <cstdio>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <string>

#include <android-base/file.h>
#include <android-base/parsedouble.h>
#include <android-base/scopeguard.h>

#include "infomanager.hpp"
#include "partitions.hpp"
#include "set_metadata.h"
#include "twcommon.h"
#include "twrp_functions.hpp"
#include "variables.h"

InfoManager::InfoManager() {
  file_version_ = 0;
  is_const_ = false;
}

InfoManager::InfoManager(const std::string& filename) {
  file_version_ = 0;
  is_const_ = false;
  SetFile(filename);
}

InfoManager::~InfoManager() {
  Clear();
}

void InfoManager::SetFile(const std::string& filename) {
  file_ = filename;
}

void InfoManager::SetFileVersion(const int version) {
  file_version_ = version;
}

void InfoManager::SetConst() {
  is_const_ = true;
}

void InfoManager::Clear() {
  values_.clear();
}

static bool twPersistFirstMounted = false;

static void twPersistMount() {
  twPersistFirstMounted = PartitionManager.Is_Mounted_By_Path(TW_PERSIST_DIR);
  if (!twPersistFirstMounted) PartitionManager.Mount_By_Path(TW_PERSIST_DIR, false);
}

static void twPersistUnMount() {
  if (!twPersistFirstMounted) PartitionManager.UnMount_By_Path(TW_PERSIST_DIR, false);
}

int InfoManager::LoadValues() {
  twPersistMount();
  auto unmount = android::base::make_scope_guard([&] { twPersistUnMount(); });
  if (!TWFunc::IsPathExists(std::string(TW_PERSIST_DIR))) mkdir(TW_PERSIST_DIR, 0777);

  std::string content;
  if (!android::base::ReadFileToString(file_, &content)) {
    LOGINFO("InfoManager file '%s' not found.\n", file_.c_str());
    return -1;
  }
  LOGINFO("InfoManager loading from '%s'.\n", file_.c_str());

  // Binary record stream: [unsigned short length][bytes...] for name, then value.
  // The on-disk length counts the trailing NUL. Read the file once and walk it with
  // a cursor — no per-record fread, no stack buffer, no feof. Length is authoritative,
  // so the field is read by count rather than scanning for a NUL.
  const char* cur = content.data();
  const char* const end = cur + content.size();

  if (file_version_) {
    if (end - cur < static_cast<ptrdiff_t>(sizeof(int))) return 0;
    int read_file_version;
    std::memcpy(&read_file_version, cur, sizeof(int));
    cur += sizeof(int);
    if (read_file_version != file_version_) {
      LOGINFO("InfoManager file version has changed, not reading file\n");
      return 0;
    }
  }

  const auto read_field = [&](std::string& out) -> bool {
    if (end - cur < static_cast<ptrdiff_t>(sizeof(unsigned short))) return false;
    unsigned short len;
    std::memcpy(&len, cur, sizeof(len));
    cur += sizeof(len);
    if (len >= 512 || cur + len > end) return false;
    out.assign(cur, len ? len - 1 : 0);  // drop the trailing NUL the length counts
    cur += len;
    return true;
  };

  std::string name, value;
  while (read_field(name) && read_field(value)) {
    values_.insert_or_assign(std::move(name), std::move(value));
  }
  return 0;
}

int InfoManager::SaveValues() {
  if (file_.empty()) return -1;

  twPersistMount();
  auto unmount = android::base::make_scope_guard([&] { twPersistUnMount(); });
  LOGINFO("InfoManager saving '%s'\n", file_.c_str());

  // Serialize the whole file into one buffer, then write it in a single pass. The
  // on-disk layout — [unsigned short length][bytes incl. trailing NUL] for name
  // then value, with an optional leading file_version — is preserved byte-for-byte.
  std::string out;
  out.reserve(values_.size() * 32 + (file_version_ ? sizeof(int) : 0));
  if (file_version_) out.append(reinterpret_cast<const char*>(&file_version_), sizeof(int));
  const auto put = [&out](const std::string& s) {
    const unsigned short len = static_cast<unsigned short>(s.length()) + 1;
    out.append(reinterpret_cast<const char*>(&len), sizeof(len));
    out.append(s);
    out.push_back('\0');
  };
  for (const auto& [key, value] : values_) {
    put(key);
    put(value);
  }

  std::unique_ptr<FILE, decltype(&fclose)> file(fopen(file_.c_str(), "wb"), fclose);
  if (!file) return -1;
  fwrite(out.data(), 1, out.size(), file.get());
  file.reset();  // flush + close before setting metadata, preserving original call order
  tw_set_default_metadata(file_.c_str());
  return 0;
}

int InfoManager::GetValue(const std::string& key, std::string& value) {
  const std::string& localStr = key;

  const auto pos = values_.find(localStr);
  if (pos == values_.end()) return -1;

  value = pos->second;
  return 0;
}

int InfoManager::GetValue(const std::string& key, int& value) {
  std::string data;

  if (GetValue(key, data) != 0) return -1;

  value = 0;
  std::from_chars(data.data(), data.data() + data.size(), value);
  return 0;
}

int InfoManager::GetValue(const std::string& key, float& value) {
  std::string data;

  if (GetValue(key, data) != 0) return -1;

  value = 0;
  android::base::ParseFloat(data, &value);
  return 0;
}

uint64_t InfoManager::GetValue(const std::string& key, uint64_t& value) {
  std::string data;

  if (GetValue(key, data) != 0) return -1;

  value = 0;
  std::from_chars(data.data(), data.data() + data.size(), value);
  return 0;
}

// This function will return an empty string if the value doesn't exist
std::string InfoManager::GetStrValue(const std::string& key) {
  std::string retVal;

  GetValue(key, retVal);
  return retVal;
}

// This function will return 0 if the value doesn't exist
int InfoManager::GetIntValue(const std::string& key) {
  std::string retVal;
  GetValue(key, retVal);
  int value = 0;
  std::from_chars(retVal.data(), retVal.data() + retVal.size(), value);
  return value;
}

int InfoManager::SetValue(const std::string& key, const std::string& value) {
  // Don't allow empty names or numerical starting values
  if (key.empty() || std::isdigit(key.front())) return -1;

  if (const auto pos = values_.find(key); pos == values_.end()) {
    values_.emplace(key, value);
  } else if (!is_const_) {
    pos->second = value;
  }

  return 0;
}

int InfoManager::SetValue(const std::string& key, const int value) {
  return SetValue(key, std::to_string(value));
}

int InfoManager::SetValue(const std::string& key, const float value) {
  return SetValue(key, std::format("{:.6g}", value));
}

int InfoManager::SetValue(const std::string& key, const uint64_t& value) {
  return SetValue(key, std::to_string(value));
}
