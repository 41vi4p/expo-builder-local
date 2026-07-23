#include "crypto.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdexcept>

#include "base64.hpp"

namespace ebl {

namespace {
constexpr int kIvLen = 12;
constexpr int kTagLen = 16;
}  // namespace

AesKey generateAesKey() {
  AesKey key{};
  if (RAND_bytes(key.data(), static_cast<int>(key.size())) != 1) {
    throw std::runtime_error("Failed to generate a random encryption key (RAND_bytes)");
  }
  return key;
}

std::string aesEncrypt(const std::string& plaintext, const AesKey& key) {
  unsigned char iv[kIvLen];
  if (RAND_bytes(iv, kIvLen) != 1) throw std::runtime_error("Failed to generate a random IV");

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("Failed to create cipher context");

  std::string ciphertext(plaintext.size(), '\0');
  int outLen = 0;
  int totalLen = 0;
  unsigned char tag[kTagLen];

  try {
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) throw std::runtime_error("init");
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) != 1) throw std::runtime_error("ivlen");
    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) != 1) throw std::runtime_error("key/iv");
    if (EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(&ciphertext[0]), &outLen,
                           reinterpret_cast<const unsigned char*>(plaintext.data()),
                           static_cast<int>(plaintext.size())) != 1) {
      throw std::runtime_error("update");
    }
    totalLen = outLen;
    if (EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&ciphertext[0]) + totalLen, &outLen) != 1) {
      throw std::runtime_error("final");
    }
    totalLen += outLen;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag) != 1) throw std::runtime_error("get tag");
  } catch (const std::exception&) {
    EVP_CIPHER_CTX_free(ctx);
    throw std::runtime_error("AES-256-GCM encryption failed");
  }
  EVP_CIPHER_CTX_free(ctx);
  ciphertext.resize(totalLen);

  std::string blob;
  blob.append(reinterpret_cast<const char*>(iv), kIvLen);
  blob.append(reinterpret_cast<const char*>(tag), kTagLen);
  blob.append(ciphertext);
  return base64Encode(blob);
}

std::string aesDecrypt(const std::string& encoded, const AesKey& key) {
  std::string blob = base64Decode(encoded);
  if (blob.size() < static_cast<size_t>(kIvLen + kTagLen)) {
    throw std::runtime_error("Encrypted value is too short to be valid");
  }
  const unsigned char* iv = reinterpret_cast<const unsigned char*>(blob.data());
  const unsigned char* tag = reinterpret_cast<const unsigned char*>(blob.data()) + kIvLen;
  const char* ciphertext = blob.data() + kIvLen + kTagLen;
  size_t ciphertextLen = blob.size() - kIvLen - kTagLen;

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) throw std::runtime_error("Failed to create cipher context");

  std::string plaintext(ciphertextLen, '\0');
  int outLen = 0;
  int totalLen = 0;
  bool ok = true;

  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) ok = false;
  if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) != 1) ok = false;
  if (ok && EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) != 1) ok = false;
  if (ok && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(&plaintext[0]), &outLen,
                              reinterpret_cast<const unsigned char*>(ciphertext),
                              static_cast<int>(ciphertextLen)) != 1) {
    ok = false;
  }
  if (ok) totalLen = outLen;
  if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, const_cast<unsigned char*>(tag)) != 1) ok = false;
  int finalLen = 0;
  bool authOk = ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(&plaintext[0]) + totalLen, &finalLen) == 1;
  EVP_CIPHER_CTX_free(ctx);

  if (!authOk) {
    throw std::runtime_error("Failed to decrypt config value — the machine key may not match, or the value is corrupt");
  }
  plaintext.resize(totalLen + finalLen);
  return plaintext;
}

}  // namespace ebl
