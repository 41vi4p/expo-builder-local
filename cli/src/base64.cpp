#include "base64.hpp"

#include <array>
#include <stdexcept>

namespace ebl {

namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::array<int, 256> buildDecodeTable() {
  std::array<int, 256> table{};
  table.fill(-1);
  for (int i = 0; i < 64; i++) table[static_cast<unsigned char>(kAlphabet[i])] = i;
  return table;
}
}  // namespace

std::string base64Encode(const std::string& raw) {
  std::string out;
  out.reserve(((raw.size() + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= raw.size()) {
    unsigned int n = (static_cast<unsigned char>(raw[i]) << 16) | (static_cast<unsigned char>(raw[i + 1]) << 8) |
                      static_cast<unsigned char>(raw[i + 2]);
    out += kAlphabet[(n >> 18) & 0x3F];
    out += kAlphabet[(n >> 12) & 0x3F];
    out += kAlphabet[(n >> 6) & 0x3F];
    out += kAlphabet[n & 0x3F];
    i += 3;
  }
  size_t remaining = raw.size() - i;
  if (remaining == 1) {
    unsigned int n = static_cast<unsigned char>(raw[i]) << 16;
    out += kAlphabet[(n >> 18) & 0x3F];
    out += kAlphabet[(n >> 12) & 0x3F];
    out += "==";
  } else if (remaining == 2) {
    unsigned int n = (static_cast<unsigned char>(raw[i]) << 16) | (static_cast<unsigned char>(raw[i + 1]) << 8);
    out += kAlphabet[(n >> 18) & 0x3F];
    out += kAlphabet[(n >> 12) & 0x3F];
    out += kAlphabet[(n >> 6) & 0x3F];
    out += '=';
  }
  return out;
}

std::string base64Decode(const std::string& encoded) {
  static const std::array<int, 256> table = buildDecodeTable();
  std::string out;
  out.reserve((encoded.size() / 4) * 3);

  int buffer = 0;
  int bits = 0;
  for (char c : encoded) {
    if (c == '=' || c == '\n' || c == '\r') continue;
    int value = table[static_cast<unsigned char>(c)];
    if (value < 0) throw std::runtime_error("Invalid base64 input");
    buffer = (buffer << 6) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xFF);
    }
  }
  return out;
}

}  // namespace ebl
