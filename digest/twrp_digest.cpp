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

#include "twrp_digest.hpp"

#include <array>

#include <android-base/hex.h>

TwrpDigest::TwrpDigest(const Algorithm algorithm)
  : md_(algorithm == Algorithm::kSha256 ? EVP_sha256() : EVP_md5()) {
  EVP_DigestInit_ex(ctx_.get(), md_, nullptr);
}

// Out of line so that only this translation unit pulls in BoringSSL's EVP_MD_CTX_cleanup: modules
// linking libtwrpdigest without libcrypto would otherwise be left with an undefined symbol
TwrpDigest::~TwrpDigest() = default;

void TwrpDigest::Reset() {
  finalized_ = false;
  EVP_DigestInit_ex(ctx_.get(), md_, nullptr);
}

void TwrpDigest::Update(const void* data, const size_t len) {
  // EVP_DigestUpdate would crash on a context whose initialization failed, and feeding a finalized
  // context would only add garbage to it
  if (finalized_ || !EVP_MD_CTX_get0_md(ctx_.get())) return;
  EVP_DigestUpdate(ctx_.get(), data, len);
}

std::string TwrpDigest::HexDigest() {
  if (finalized_ || !EVP_MD_CTX_get0_md(ctx_.get())) return {};

  std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
  unsigned int hash_len = 0;
  EVP_DigestFinal_ex(ctx_.get(), hash.data(), &hash_len);
  finalized_ = true;

  // Lowercase byte pairs, i.e. exactly the layout md5sum(1) and sha256sum(1) print
  return android::base::HexString(hash.data(), hash_len);
}