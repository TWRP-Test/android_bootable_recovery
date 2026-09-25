/*
	Copyright 2013 to 2017 TeamWin
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

#include "twrp_digest_driver.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>

#include <algorithm>
#include <cstddef>
#include <format>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <android-base/file.h>
#include <android-base/macros.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

#include "data.hpp"
#include "gui/gui.hpp"
#include "set_metadata.h"
#include "twcommon.h"
#include "twrp_functions.hpp"
#include "twrp_digest.hpp"
#include "variables.h"

namespace {
// "SHA2"/"MD5" for logs.
const char* AlgorithmName(const TwrpDigest::Algorithm algorithm) {
  return algorithm == TwrpDigest::Algorithm::kSha256 ? "SHA2" : "MD5";
}

constexpr bool IsHexDigit(const char c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// A parsed digest-file line (md5sum/sha256sum "<hex>  <name>" format).
struct StoredDigest {
  std::string hex; // raw; compared case-insensitively (md5sum -c, strcasecmp)
  std::string name;
  TwrpDigest::Algorithm algorithm; // from hex length, not the suffix
};

// Parses one line; nullopt if not 32/64 hex digits + a name. Accepts CRLF, uppercase, '*'.
std::optional<StoredDigest> ParseStoredDigest(const std::string_view content) {
  std::string_view line = content.substr(0, content.find('\n'));
  while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.
      remove_suffix(1);

  const size_t hex_end = line.find_first_of(" \t");
  if (hex_end == std::string_view::npos) return std::nullopt;

  std::string_view name = line.substr(hex_end);
  const size_t name_start = name.find_first_not_of(" \t");
  if (name_start == std::string_view::npos) return std::nullopt;
  name.remove_prefix(name_start);
  if (name.starts_with('*')) name.remove_prefix(1);
  if (name.empty()) return std::nullopt;

  const std::string_view hex = line.substr(0, hex_end);
  const bool is_md5 = hex.size() == TwrpDigestDriver::kMd5HexSize;
  if (!is_md5 && hex.size() != TwrpDigestDriver::kSha256HexSize) return std::nullopt;
  // Fail fast on non-hex; the stored case is left as-is (compared case-insensitively).
  if (!std::ranges::all_of(hex, IsHexDigit)) return std::nullopt;

  const TwrpDigest::Algorithm algorithm =
      is_md5 ? TwrpDigest::Algorithm::kMd5 : TwrpDigest::Algorithm::kSha256;
  return StoredDigest{ std::string(hex), std::string(name), algorithm };
}
} // namespace

bool TwrpDigestDriver::CheckFileDigest(const std::string& filename) {
  bool found = false;
  std::string stored_digest;

  // open()+ReadFdToString: one syscall, no exists/reopen race.
  for (const char* suffix : kDigestSuffixes) {
    const std::string path = filename + suffix;
    const android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(path.c_str(), O_RDONLY | O_CLOEXEC)));
    if (fd < 0) {
      // ENOENT = try next suffix; other errors must not pass for "no digest file".
      if (errno == ENOENT) continue;
      gui_msg("digest_error=Digest Error!");
      return false;
    }
    // ReadFdToString clears stored_digest (no pre-clear needed).
    if (!android::base::ReadFdToString(fd.get(), &stored_digest)) {
      gui_msg("digest_error=Digest Error!");
      return false;
    }
    found = true;
    break;
  }

  if (!found) {
    gui_msg(Msg(msg::kWarning, "no_digest=Skipping Digest check: no Digest file found"));
    return true;
  }

  const std::optional<StoredDigest> stored = ParseStoredDigest(stored_digest);
  if (!stored) {
    gui_msg("digest_error=Digest Error!");
    return false;
  }

  // Bind by basename before the multi-GB hash.
  const std::string file_name = TWFunc::GetFilename(filename);
  if (TWFunc::GetFilename(stored->name) != file_name) {
    gui_msg(Msg(msg::kError, "digest_fail_match=Digest failed to match on '{1}'.")(filename));
    return false;
  }

  TwrpDigest digest(stored->algorithm);
  if (!StreamFileToDigest(filename, digest)) return false;

  // Empty HexDigest (hash failed) can't equal a 32/64-char stored hex.
  const std::string computed_digest = digest.HexDigest();
  if (!android::base::EqualsIgnoreCase(computed_digest, stored->hex)) {
    gui_msg(Msg(msg::kError, "digest_fail_match=Digest failed to match on '{1}'.")(filename));
    return false;
  }

  LOGINFO("%s Digest: %s  %s\n", AlgorithmName(stored->algorithm), computed_digest.c_str(),
          file_name.c_str());
  gui_msg(Msg("digest_matched=Digest matched for '{1}'.")(filename));
  return true;
}

bool TwrpDigestDriver::CheckDigest(const std::string& full_filename) {
  sync();
  if (TWFunc::IsPathExists(full_filename)) return CheckFileDigest(full_filename);
  // Single file archive

  // Split archive: check each existing part.
  for (const int index : std::views::iota(0, kMaxSplitArchives)) {
    const std::string split_filename = std::format("{}{:03}", full_filename, index);
    if (!TWFunc::IsPathExists(split_filename)) break;
    LOGINFO("split_filename: %s\n", split_filename.c_str());
    if (!CheckFileDigest(split_filename)) return false;
  }
  return true;
}

bool TwrpDigestDriver::WriteDigest(const std::string& full_filename) {
  int use_sha2 = 0;
  DataManager::GetValue(TW_USE_SHA2, use_sha2);

  const TwrpDigest::Algorithm algorithm =
      use_sha2 ? TwrpDigest::Algorithm::kSha256 : TwrpDigest::Algorithm::kMd5;
  const std::string digest_filename = full_filename + (use_sha2 ? ".sha2" : ".md5");

  TwrpDigest digest(algorithm);
  if (!StreamFileToDigest(full_filename, digest)) return false;

  const std::string digest_str = digest.HexDigest();
  if (digest_str.empty()) return false;

  // Hoisted: std::format needs lvalue args, and the name is logged too.
  const std::string file_name = TWFunc::GetFilename(full_filename);
  LOGINFO("%s Digest: %s  %s\n", AlgorithmName(algorithm), digest_str.c_str(), file_name.c_str());
  LOGINFO("digest_filename: %s\n", digest_filename.c_str());

  if (!TWFunc::WriteToFile(digest_filename, std::format("{}  {}\n", digest_str, file_name))) {
    gui_err("digest_error= * Digest Error!");
    return false;
  }

  tw_set_default_metadata(digest_filename.c_str());
  gui_msg("digest_created= * Digest Created.");
  return true;
}

bool TwrpDigestDriver::MakeDigest(const std::string& full_filename) {
  TWFunc::GuiOperationText(TW_GENERATE_DIGEST_TEXT, gui_parse_text("{@generating_digest1}"));
  gui_msg("generating_digest2= * Generating digest...");

  if (TWFunc::IsPathExists(full_filename)) return WriteDigest(full_filename);

  // Split archive: part 0 must exist (else nothing to digest).
  const std::string first_part = std::format("{}{:03}", full_filename, 0);
  if (!TWFunc::IsPathExists(first_part)) {
    LOGERR("Backup file: '%s' not found!\n", first_part.c_str());
    return false;
  }
  if (!WriteDigest(first_part)) return false;

  for (const int index : std::views::iota(1, kMaxSplitArchives)) {
    const std::string split_filename = std::format("{}{:03}", full_filename, index);
    if (!TWFunc::IsPathExists(split_filename)) break;
    if (!WriteDigest(split_filename)) return false;
  }

  gui_msg("digest_created= * Digest Created.");
  return true;
}

bool TwrpDigestDriver::StreamFileToDigest(const std::string& filename, TwrpDigest& digest) {
  const android::base::unique_fd fd(
      TEMP_FAILURE_RETRY(open(filename.c_str(), O_RDONLY | O_CLOEXEC)));
  if (fd < 0) return false;

  // Read-ahead hint for sequential IO.
  posix_fadvise(fd.get(), 0, 0, POSIX_FADV_SEQUENTIAL);

  std::vector<unsigned char> buffer(kDigestBufferSize);
  while (true) {
    const ssize_t bytes = TEMP_FAILURE_RETRY(read(fd.get(), buffer.data(), buffer.size()));
    // 0 = EOF (ok), < 0 = error.
    if (bytes <= 0) return bytes == 0;
    digest.Update(buffer.data(), static_cast<size_t>(bytes));
  }
}