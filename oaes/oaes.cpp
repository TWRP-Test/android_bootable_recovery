#include "oaes.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

namespace Oaes {

static const uint8_t kHeader[16] = {
	0x4f, 0x41, 0x45, 0x53, 0x01, 0x02, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const EVP_CIPHER* Cipher(size_t key_len) {
	if (key_len == 16) return EVP_aes_128_cbc();
	if (key_len == 24) return EVP_aes_192_cbc();
	return EVP_aes_256_cbc();
}

static void PrepareKey(const std::string& password, uint8_t* key, size_t* key_len) {
	for (size_t i = 0; i < 32; ++i) key[i] = i + 1;
	*key_len = password.size() <= 16 ? 16 : password.size() <= 24 ? 24 : 32;
	memcpy(key, password.data(), std::min(password.size(), size_t(32)));
}

static ssize_t ReadFull(int fd, uint8_t* buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t n = read(fd, buf + total, len - total);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return total ? (ssize_t)total : n;
		total += n;
	}
	return (ssize_t)total;
}

static ssize_t WriteFull(int fd, const uint8_t* buf, size_t len) {
	size_t total = 0;
	while (total < len) {
		ssize_t n = write(fd, buf + total, len - total);
		if (n < 0 && errno == EINTR) continue;
		if (n <= 0) return -1;
		total += n;
	}
	return (ssize_t)total;
}

static ssize_t DecryptChunk(const uint8_t* chunk, size_t chunk_len, uint8_t* out,
		size_t out_cap, const EVP_CIPHER* cipher, const uint8_t* key) {
	if (chunk_len < 32 || memcmp(chunk, kHeader, 4) != 0 || chunk[4] != 1 || chunk[5] != 2)
		return -1;
	if ((chunk[6] | (chunk[7] << 8)) != 2 || (chunk[8] & ~1)) return -1;
	size_t cipher_len = chunk_len - 32;
	if (cipher_len % 16 != 0 || cipher_len > out_cap) return -1;
	EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
	if (!ctx) return -1;
	EVP_DecryptInit_ex(ctx, cipher, NULL, key, chunk + 16);
	EVP_CIPHER_CTX_set_padding(ctx, 0);
	int out_len = 0, final_len = 0;
	bool ok = EVP_DecryptUpdate(ctx, out, &out_len, chunk + 32, cipher_len) &&
		EVP_DecryptFinal_ex(ctx, out + out_len, &final_len);
	EVP_CIPHER_CTX_free(ctx);
	if (!ok) return -1;
	size_t plain_len = out_len + final_len;
	if (chunk[8]) {
		if (!plain_len) return -1;
		uint8_t pad_len = out[plain_len - 1];
		if (!pad_len || pad_len > 15 || pad_len > plain_len) return -1;
		for (size_t i = 0; i < pad_len; ++i)
			if (out[plain_len - 1 - i] != pad_len - i) return -1;
		plain_len -= pad_len;
	}
	return (ssize_t)plain_len;
}

int TryDecryptingFile(const std::string& filename, const std::string& password) {
	uint8_t key[32]; size_t key_len;
	PrepareKey(password, key, &key_len);
	FILE* file = fopen(filename.c_str(), "rb");
	if (!file) return -1;
	uint8_t input[4096];
	size_t input_len = fread(input, 1, sizeof(input), file);
	fclose(file);
	if (!input_len) return -1;
	if (input_len < 32) return 0;
	uint8_t* output = (uint8_t*)calloc(input_len, 1);
	if (!output) return -1;
	ssize_t plain_len = DecryptChunk(input, input_len, output, input_len, Cipher(key_len), key);
	if (plain_len < 0) { free(output); return 0; }
	if (plain_len >= 2 && output[0] == 0x1f && output[1] == 0x8b) { free(output); return 3; }
	if (plain_len >= 262 && !strncmp((char*)output + 257, "ustar", 5)) { free(output); return 2; }
	free(output);
	return 1;
}

void EncryptStream(const std::string& password) {
	uint8_t key[32]; size_t key_len;
	PrepareKey(password, key, &key_len);
	const EVP_CIPHER* cipher = Cipher(key_len);
	uint8_t input[4064], output[4096];
	uint8_t iv[16];
	int random_fd = open("/dev/urandom", O_RDONLY);
	if (random_fd >= 0) { ReadFull(random_fd, iv, sizeof(iv)); close(random_fd); }
	else memset(iv, 0, sizeof(iv));
	ssize_t n;
	while ((n = ReadFull(STDIN_FILENO, input, sizeof(input))) > 0) {
		memcpy(output, kHeader, 16); output[6] = 2;
		size_t pad = (16 - (n % 16)) % 16;
		output[8] = pad ? 1 : 0;
		memcpy(output + 16, iv, sizeof(iv));
		for (size_t i = 0; i < pad; ++i) input[n + i] = i + 1;
		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (!ctx) return;
		EVP_EncryptInit_ex(ctx, cipher, NULL, key, iv);
		EVP_CIPHER_CTX_set_padding(ctx, 0);
		int out_len = 0, final_len = 0;
		EVP_EncryptUpdate(ctx, output + 32, &out_len, input, n + pad);
		EVP_EncryptFinal_ex(ctx, output + 32 + out_len, &final_len);
		EVP_CIPHER_CTX_free(ctx);
		if (WriteFull(STDOUT_FILENO, output, 32 + out_len + final_len) < 0) return;
		memcpy(iv, output + 32 + out_len + final_len - sizeof(iv), sizeof(iv));
	}
}

void DecryptStream(const std::string& password) {
	uint8_t key[32]; size_t key_len;
	PrepareKey(password, key, &key_len);
	const EVP_CIPHER* cipher = Cipher(key_len);
	uint8_t input[4096], output[4096];
	ssize_t n;
	while ((n = ReadFull(STDIN_FILENO, input, sizeof(input))) > 0) {
		ssize_t plain_len = DecryptChunk(input, n, output, sizeof(output), cipher, key);
		if (plain_len < 0 || WriteFull(STDOUT_FILENO, output, plain_len) < 0) return;
	}
}

}
