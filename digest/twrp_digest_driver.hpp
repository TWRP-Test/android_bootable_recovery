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

#ifndef TWRP_DIGEST_DRIVER_HPP
#define TWRP_DIGEST_DRIVER_HPP

#include <cstddef>
#include <string>

class TwrpDigest;

// Creates and verifies digest files stored next to partition backups.
class TwrpDigestDriver {
public:
  // Tries .sha2/.sha256/.md5/.md5sum; a missing digest skips the check (returns true).
  static bool CheckFileDigest(const std::string& filename);

  // Verifies full_filename, or each part of a split archive.
  static bool CheckDigest(const std::string& full_filename);

  // Writes full_filename's digest to .sha2 or .md5 per tw_use_sha2.
  static bool WriteDigest(const std::string& full_filename);

  // Writes the digest of full_filename, or each split-archive part.
  static bool MakeDigest(const std::string& full_filename);

  static constexpr int kMaxSplitArchives = 1000;
  static constexpr size_t kDigestBufferSize = 256 * 1024;
  static constexpr size_t kMd5HexSize = 32;
  static constexpr size_t kSha256HexSize = 64;
  // Probe order only; the hex length (32/64) decides MD5 vs SHA-256.
  static constexpr const char* kDigestSuffixes[] = { ".sha2", ".sha256", ".md5", ".md5sum" };

private:
  // Streams filename into digest in 256 KiB chunks; O(1) memory.
  static bool StreamFileToDigest(const std::string& filename, TwrpDigest& digest);
};

#endif  // TWRP_DIGEST_DRIVER_HPP