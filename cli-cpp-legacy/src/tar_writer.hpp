#pragma once
// Packs a directory into an in-memory USTAR tar archive — the request body format
// Docker's `POST /build` endpoint expects for a build context. Only regular files are
// included (Docker/Go's archive/tar build-context reader creates parent directories
// implicitly), which is all the bundled runner context (Dockerfile + shell/JS
// scripts, no nested subdirectories of consequence) needs.
#include <string>

namespace ebl {

std::string createTarFromDirectory(const std::string& dirPath);

}  // namespace ebl
