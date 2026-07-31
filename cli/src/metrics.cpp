#include "metrics.hpp"

#include <openssl/evp.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace ebl {

namespace {

std::string sha256File(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot open artifact for hashing: " + path);

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx || EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
    if (ctx) EVP_MD_CTX_free(ctx);
    throw std::runtime_error("Failed to initialize SHA-256 digest");
  }

  std::array<char, 1 << 16> buffer{};
  while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
    EVP_DigestUpdate(ctx, buffer.data(), static_cast<size_t>(file.gcount()));
  }

  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digestLen = 0;
  EVP_DigestFinal_ex(ctx, digest, &digestLen);
  EVP_MD_CTX_free(ctx);

  static const char* hex = "0123456789abcdef";
  std::string out;
  out.reserve(digestLen * 2);
  for (unsigned int i = 0; i < digestLen; i++) {
    out += hex[(digest[i] >> 4) & 0xF];
    out += hex[digest[i] & 0xF];
  }
  return out;
}

std::string readFileToString(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return "";
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

#ifdef _WIN32
// CreateProcess takes one command-line string, not argv — quote each argument per
// the documented Windows argv-quoting rules (only `git` args ever pass through
// here, but this handles the general case regardless).
std::string quoteWindowsArg(const std::string& a) {
  if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos) return a;
  std::string out = "\"";
  size_t backslashes = 0;
  for (char c : a) {
    if (c == '\\') {
      backslashes++;
      continue;
    }
    if (c == '"') {
      out.append(backslashes * 2 + 1, '\\');
    } else {
      out.append(backslashes, '\\');
    }
    backslashes = 0;
    out += c;
  }
  out.append(backslashes * 2, '\\');
  out += '"';
  return out;
}
#endif

/** Runs a command with argv directly (no shell involved) and returns its trimmed
 * stdout, or an empty string if it exits non-zero or can't be spawned. Used only for
 * `git`, which is optional metadata — never fatal to the build if unavailable. */
std::string runCommandCapture(const std::vector<std::string>& args) {
#ifdef _WIN32
  if (args.empty()) return "";

  std::string cmdLine;
  for (size_t i = 0; i < args.size(); i++) {
    if (i > 0) cmdLine += ' ';
    cmdLine += quoteWindowsArg(args[i]);
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!::CreatePipe(&readPipe, &writePipe, &sa, 0)) return "";
  ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  // git's stderr is discarded, same as the unix version redirecting it to /dev/null.
  HANDLE nulHandle =
      ::CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writePipe;
  si.hStdError = nulHandle;
  si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};
  std::vector<char> cmdLineBuf(cmdLine.begin(), cmdLine.end());
  cmdLineBuf.push_back('\0');
  BOOL ok = ::CreateProcessA(nullptr, cmdLineBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                              &si, &pi);
  ::CloseHandle(writePipe);
  if (nulHandle != INVALID_HANDLE_VALUE && nulHandle != nullptr) ::CloseHandle(nulHandle);
  if (!ok) {
    ::CloseHandle(readPipe);
    return "";
  }

  std::string output;
  char buf[256];
  DWORD n = 0;
  while (::ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) output.append(buf, n);
  ::CloseHandle(readPipe);

  ::WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitCode = 1;
  ::GetExitCodeProcess(pi.hProcess, &exitCode);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
  if (exitCode != 0) return "";
#else
  int pipefd[2];
  if (pipe(pipefd) != 0) return "";

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return "";
  }

  if (pid == 0) {
    // child
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    int devNull = open("/dev/null", O_WRONLY);
    if (devNull >= 0) dup2(devNull, STDERR_FILENO);
    close(pipefd[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);  // execvp only returns on failure
  }

  // parent
  close(pipefd[1]);
  std::string output;
  char buf[256];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) output.append(buf, n);
  close(pipefd[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return "";
#endif

  while (!output.empty() && (output.back() == '\n' || output.back() == '\r')) output.pop_back();
  return output;
}

struct GradleValues {
  std::string applicationId;
  std::string versionCode;
  std::string versionName;
};

GradleValues readGradleManifestValues(const std::string& appPath) {
  GradleValues values;
  std::string path = appPath + "/android/app/build.gradle";
  std::string src = readFileToString(path);
  if (src.empty()) return values;

  std::smatch match;
  if (std::regex_search(src, match, std::regex(R"(applicationId\s+["']([^"']+)["'])"))) {
    values.applicationId = match[1].str();
  }
  if (std::regex_search(src, match, std::regex(R"(versionCode\s+(\d+))"))) {
    values.versionCode = match[1].str();
  }
  if (std::regex_search(src, match, std::regex(R"(versionName\s+["']([^"']+)["'])"))) {
    values.versionName = match[1].str();
  }
  return values;
}

std::string readPackageJsonVersion(const std::string& appPath) {
  std::string src = readFileToString(appPath + "/package.json");
  std::smatch match;
  if (std::regex_search(src, match, std::regex(R"re("version"\s*:\s*"([^"]+)")re"))) {
    return match[1].str();
  }
  return "";
}

}  // namespace

ArtifactMetrics extractArtifactMetrics(const std::string& appPath, const std::string& artifactPath) {
  ArtifactMetrics metrics;

  std::error_code ec;
  uintmax_t size = fs::file_size(artifactPath, ec);
  if (ec) throw std::runtime_error("Artifact not found at " + artifactPath);
  metrics.sizeBytes = static_cast<uint64_t>(size);
  metrics.sha256 = sha256File(artifactPath);

  GradleValues gradle = readGradleManifestValues(appPath);
  metrics.applicationId = gradle.applicationId;
  metrics.versionCode = gradle.versionCode;
  metrics.versionName = !gradle.versionName.empty() ? gradle.versionName : readPackageJsonVersion(appPath);

  metrics.gitCommit = runCommandCapture({"git", "-C", appPath, "rev-parse", "--short", "HEAD"});
  metrics.gitBranch = runCommandCapture({"git", "-C", appPath, "rev-parse", "--abbrev-ref", "HEAD"});

  return metrics;
}

}  // namespace ebl
