#include "host_process.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cstdlib>

namespace ebl {

#ifdef _WIN32

namespace {

// Same argv -> single-command-line quoting as metrics.cpp's quoteWindowsArg -
// duplicated rather than shared across translation units for a few lines of code,
// consistent with detect.cpp/metrics.cpp's own "keep in sync by hand" convention
// for small platform helpers in this codebase.
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

}  // namespace

int runHostProcessStreaming(const std::vector<std::string>& args, const std::string& workingDir,
                             const std::function<void(const char*, size_t)>& onChunk) {
  if (args.empty()) return -1;

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
  if (!::CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
  ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writePipe;
  si.hStdError = writePipe;  // combined stdout+stderr, unlike metrics.cpp's git capture
  si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

  PROCESS_INFORMATION pi{};
  std::vector<char> cmdLineBuf(cmdLine.begin(), cmdLine.end());
  cmdLineBuf.push_back('\0');
  BOOL ok = ::CreateProcessA(nullptr, cmdLineBuf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                              workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi);
  ::CloseHandle(writePipe);
  if (!ok) {
    ::CloseHandle(readPipe);
    return -1;
  }

  char buf[4096];
  DWORD n = 0;
  while (::ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
    onChunk(buf, static_cast<size_t>(n));
  }
  ::CloseHandle(readPipe);

  ::WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exitCode = 1;
  ::GetExitCodeProcess(pi.hProcess, &exitCode);
  ::CloseHandle(pi.hProcess);
  ::CloseHandle(pi.hThread);
  return static_cast<int>(exitCode);
}

#else

int runHostProcessStreaming(const std::vector<std::string>& args, const std::string& workingDir,
                             const std::function<void(const char*, size_t)>& onChunk) {
  if (args.empty()) return -1;

  int pipefd[2];
  if (pipe(pipefd) != 0) return -1;

  pid_t pid = fork();
  if (pid < 0) {
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  if (pid == 0) {
    // child
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    if (!workingDir.empty() && chdir(workingDir.c_str()) != 0) _exit(127);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    execvp(argv[0], argv.data());
    _exit(127);  // execvp only returns on failure - "command not found" convention
  }

  // parent
  close(pipefd[1]);
  char buf[4096];
  ssize_t n;
  while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
    onChunk(buf, static_cast<size_t>(n));
  }
  close(pipefd[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status)) return WEXITSTATUS(status);
  if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
  return 1;
}

#endif

}  // namespace ebl
