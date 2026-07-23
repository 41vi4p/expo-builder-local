#pragma once
#include <string>

namespace ebl {

std::string base64Encode(const std::string& raw);
std::string base64Decode(const std::string& encoded);

}  // namespace ebl
