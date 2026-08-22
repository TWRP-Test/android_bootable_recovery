#ifndef TWRP_OAES_HPP
#define TWRP_OAES_HPP

#include <string>

namespace Oaes {
int TryDecryptingFile(const std::string& filename, const std::string& password);
void EncryptStream(const std::string& password);
void DecryptStream(const std::string& password);
}

#endif
