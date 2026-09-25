/*
	Copyright 2012 to 2017 TeamWin
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

#ifndef TWRP_DIGEST_HPP
#define TWRP_DIGEST_HPP

#include <cstddef>
#include <string>

#include <openssl/digest.h>

// Streaming hash for one of the two algorithms TWRP writes next to its backups
class TwrpDigest {
public:
  // Algorithms TWRP stores as the .sha2 and .md5 digest file suffixes
  enum class Algorithm { kMd5, kSha256 };

  // Prepares the hash state for algorithm
  explicit TwrpDigest(Algorithm algorithm);

  // Out of line so BoringSSL's EVP_MD_CTX cleanup is emitted only in libtwrpdigest.
  ~TwrpDigest();

  TwrpDigest(const TwrpDigest&) = delete;

  TwrpDigest& operator=(const TwrpDigest&) = delete;

  // Discards everything hashed so far so that a new stream can be fed in
  void Reset();

  // Hashes len bytes from data
  void Update(const void* data, size_t len);

  // Lowercase hex string of everything hashed so far, as md5sum(1) prints it. A digest can only be
  // read once per Reset(); later calls return an empty string
  std::string HexDigest();

private:
  const EVP_MD* md_{};
  bssl::ScopedEVP_MD_CTX ctx_{};
  bool finalized_ = false;
};

#endif  // TWRP_DIGEST_HPP