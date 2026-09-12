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

#ifndef INFO_MANAGER_HPP_HEADER
#define INFO_MANAGER_HPP_HEADER

#include <cstdint>
#include <map>
#include <string>
#include <utility>

class InfoManager {
public:
  InfoManager();

  explicit InfoManager(const std::string& filename);

  virtual ~InfoManager();

  void SetFile(const std::string& filename);

  void SetFileVersion(int version);

  void SetConst();

  void Clear();

  int LoadValues();

  int SaveValues();

  // Core get routines
  int GetValue(const std::string& key, std::string& value);

  int GetValue(const std::string& key, int& value);

  int GetValue(const std::string& key, float& value);

  uint64_t GetValue(const std::string& key, uint64_t& value);

  std::string GetStrValue(const std::string& key);

  int GetIntValue(const std::string& key);

  // Core set routines
  int SetValue(const std::string& key, const std::string& value);

  int SetValue(const std::string& key, int value);

  int SetValue(const std::string& key, float value);

  int SetValue(const std::string& key, const uint64_t& value);

  // Map-style sugar: info["tw_key"] = "v";  std::string v = info["tw_key"];  int i = info.GetIntValue("tw_key");
  // operator[] returns a Handle proxy (the store holds std::string, but int/float/uint64_t writes
  // route through SetValue, honoring is_const + the leading-digit guard). Reads convert to std::string
  // only — "" on missing, no insert-on-read (via GetStrValue). No implicit numeric op: it'd fire in
  // if()/arithmetic and erase the missing-key (-1) signal GetIntValue/GetValue preserve.
  class Handle {
  public:
    Handle(InfoManager& owner, std::string key) : owner_(owner), key_(std::move(key)) {
    }

    Handle& operator=(const char* v) {
      owner_.SetValue(key_, std::string{ v });
      return *this;
    }

    Handle& operator=(const std::string& v) {
      owner_.SetValue(key_, v);
      return *this;
    }

    Handle& operator=(const bool v) {
      owner_.SetValue(key_, v);
      return *this;
    }

    Handle& operator=(const int v) {
      owner_.SetValue(key_, v);
      return *this;
    }

    Handle& operator=(const float v) {
      owner_.SetValue(key_, v);
      return *this;
    }

    Handle& operator=(const uint64_t v) {
      owner_.SetValue(key_, v);
      return *this;
    }

    operator std::optional<std::string>() const {
      if (const auto pos = owner_.values_.find(key_); pos != owner_.values_.end()) {
        return pos->second;
      }
      return std::nullopt;
    }

    operator std::optional<int>() const {
      if (const auto pos = owner_.values_.find(key_); pos != owner_.values_.end()) {
        return std::stoi(pos->second);
      }
      return std::nullopt;
    }

    operator std::optional<bool>() const {
      if (const auto pos = owner_.values_.find(key_); pos != owner_.values_.end()) {
        return std::stoi(pos->second) == 1;
      }
      return std::nullopt;
    }

    operator std::optional<float>() const {
      if (const auto pos = owner_.values_.find(key_); pos != owner_.values_.end()) {
        return std::stof(pos->second);
      }
      return std::nullopt;
    }

    operator std::optional<uint64_t>() const {
      if (const auto pos = owner_.values_.find(key_); pos != owner_.values_.end()) {
        return std::stoul(pos->second);
      }
      return std::nullopt;
    }

  private:
    InfoManager& owner_;
    std::string key_;
  };

  Handle operator[](std::string key) {
    return Handle{ *this, std::move(key) };
  }

private:
  std::string file_;
  std::map<std::string, std::string> values_;
  int file_version_;
  bool is_const_;
};

#endif // INFO_MANAGER_HPP_HEADER
