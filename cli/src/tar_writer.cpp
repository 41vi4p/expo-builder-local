#include "tar_writer.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace ebl {

namespace {

// POSIX ustar header layout (512 bytes total):
//   0    100  name
//   100  8    mode      (octal ASCII, NUL-terminated)
//   108  8    uid
//   116  8    gid
//   124  12   size      (octal ASCII, NUL-terminated)
//   136  12   mtime
//   148  8    checksum  (6 octal digits, NUL, space)
//   156  1    typeflag  ('0' = regular file)
//   157  100  linkname
//   257  6    magic     "ustar\0"
//   263  2    version   "00"
//   265  32   uname
//   297  32   gname
//   329  8    devmajor
//   337  8    devminor
//   345  155  prefix
//   500  12   (padding)

constexpr size_t kBlockSize = 512;

void writeOctalField(char* field, size_t fieldSize, unsigned long long value) {
  std::snprintf(field, fieldSize, "%0*llo", static_cast<int>(fieldSize - 1), value);
}

std::string makeHeader(const std::string& relPath, unsigned long long size, unsigned int mode) {
  if (relPath.size() >= 100) {
    throw std::runtime_error("tar_writer: path too long for a plain ustar name field: " + relPath);
  }

  char header[kBlockSize];
  std::memset(header, 0, sizeof(header));

  std::memcpy(header, relPath.data(), relPath.size());
  writeOctalField(header + 100, 8, mode & 0777ULL);
  writeOctalField(header + 108, 8, 0);
  writeOctalField(header + 116, 8, 0);
  writeOctalField(header + 124, 12, size);
  writeOctalField(header + 136, 12, 0);
  std::memset(header + 148, ' ', 8);  // checksum field, provisionally spaces
  header[156] = '0';                  // typeflag: regular file
  std::memcpy(header + 257, "ustar", 6);  // "ustar" + implicit NUL from the literal
  header[263] = '0';
  header[264] = '0';

  unsigned long checksum = 0;
  for (unsigned char c : std::string(header, kBlockSize)) checksum += c;
  char checksumField[8];
  std::snprintf(checksumField, sizeof(checksumField), "%06lo", checksum);
  checksumField[6] = '\0';
  checksumField[7] = ' ';
  std::memcpy(header + 148, checksumField, 8);

  return std::string(header, kBlockSize);
}

void appendPadded(std::string& tar, const std::string& content) {
  tar += content;
  size_t pad = (kBlockSize - (content.size() % kBlockSize)) % kBlockSize;
  tar.append(pad, '\0');
}

}  // namespace

std::string createTarFromDirectory(const std::string& dirPath) {
  fs::path root(dirPath);
  if (!fs::exists(root) || !fs::is_directory(root)) {
    throw std::runtime_error("tar_writer: not a directory: " + dirPath);
  }

  std::string tar;
  for (const auto& entry : fs::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;

    fs::path relative = fs::relative(entry.path(), root);
    std::string relStr = relative.generic_string();

    struct stat st{};
    if (::stat(entry.path().c_str(), &st) != 0) {
      throw std::runtime_error("tar_writer: stat failed for " + entry.path().string());
    }

    std::ifstream file(entry.path(), std::ios::binary);
    if (!file) throw std::runtime_error("tar_writer: cannot open " + entry.path().string());
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    tar += makeHeader(relStr, content.size(), st.st_mode);
    appendPadded(tar, content);
  }

  tar.append(2 * kBlockSize, '\0');  // two zero blocks terminate the archive
  return tar;
}

}  // namespace ebl
