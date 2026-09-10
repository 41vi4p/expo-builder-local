#pragma once
// AES-256-GCM at-rest encryption for the CLI's locally-stored config secrets (Expo
// token, generated MASTER_KEY for the orchestrator). Mirrors the same scheme
// orchestrator/src/util/crypto.ts uses for keystore passwords: a random 32-byte key
// (here, stored in ~/.config/ebl/machine.key, 0600) encrypts each value with a fresh
// random 12-byte IV; the stored blob is base64(iv || tag || ciphertext).
#include <array>
#include <string>

namespace ebl {

using AesKey = std::array<unsigned char, 32>;

AesKey generateAesKey();
std::string aesEncrypt(const std::string& plaintext, const AesKey& key);
std::string aesDecrypt(const std::string& encoded, const AesKey& key);

}  // namespace ebl
