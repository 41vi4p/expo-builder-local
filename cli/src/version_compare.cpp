#include "version_compare.hpp"

#include <algorithm>
#include <sstream>
#include <vector>

namespace ebl {

namespace {

std::string stripLeadingV(const std::string& s) { return (!s.empty() && s[0] == 'v') ? s.substr(1) : s; }

std::vector<int> versionParts(const std::string& s) {
  std::vector<int> parts;
  std::stringstream ss(s);
  std::string part;
  while (std::getline(ss, part, '.')) {
    try {
      parts.push_back(std::stoi(part));
    } catch (const std::exception&) {
      parts.push_back(0);
    }
  }
  return parts;
}

}  // namespace

bool isVersionNewer(const std::string& a, const std::string& b) {
  std::vector<int> pa = versionParts(stripLeadingV(a));
  std::vector<int> pb = versionParts(stripLeadingV(b));
  size_t n = std::max(pa.size(), pb.size());
  for (size_t i = 0; i < n; i++) {
    int av = i < pa.size() ? pa[i] : 0;
    int bv = i < pb.size() ? pb[i] : 0;
    if (av != bv) return av > bv;
  }
  return false;
}

}  // namespace ebl
