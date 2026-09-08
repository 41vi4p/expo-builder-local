#include "native_process.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace ebl {

namespace {

/** Reads the current process's own environment block and layers `overrides`
 * ("NAME=value" strings) on top — replacing an existing NAME= entry
 * case-insensitively (Windows env var names are case-insensitive) or appending it.
 * Returns a CreateProcessA-shaped block: "NAME=value\0" entries, double-NUL
 * terminated. Passing nullptr to CreateProcessA instead would inherit the parent's
 * block unchanged, which isn't enough here — every caller needs to inherit *and*
 * add a few vars (JAVA_HOME, PATH prepends, EXPO_TOKEN, ...). */
std::string buildEnvironmentBlock(const std::vector<std::string>& overrides) {
  LPCH base = ::GetEnvironmentStringsA();
  if (!base) throw std::runtime_error("Could not read the current environment block");

  std::vector<std::string> vars;
  for (LPCH p = base; *p;) {
    std::string entry(p);
    p += entry.size() + 1;
    vars.push_back(std::move(entry));
  }
  ::FreeEnvironmentStringsA(base);

  for (const auto& ov : overrides) {
    size_t eq = ov.find('=');
    if (eq == std::string::npos) continue;
    std::string name = ov.substr(0, eq);
    bool replaced = false;
    for (auto& v : vars) {
      size_t veq = v.find('=');
      if (veq == name.size() && _strnicmp(v.c_str(), name.c_str(), name.size()) == 0) {
        v = ov;
        replaced = true;
        break;
      }
    }
    if (!replaced) vars.push_back(ov);
  }

  std::string block;
  for (const auto& v : vars) {
    block += v;
    block += '\0';
  }
  block += '\0';
  return block;
}

struct JobHandle {
  HANDLE job = nullptr;
  JobHandle() {
    job = ::CreateJobObjectA(nullptr, nullptr);
    if (job) {
      // Killing the job (TerminateJobObject) or even just closing this handle with
      // no other reference left brings down every process it still contains — the
      // whole point of using a Job Object here instead of tracking/killing each
      // child process individually the way build-entrypoint.sh's kill_tree has to.
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
      jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }
  }
  ~JobHandle() {
    if (job) ::CloseHandle(job);
  }
  JobHandle(const JobHandle&) = delete;
  JobHandle& operator=(const JobHandle&) = delete;
};

/** Cumulative CPU time (100ns units) across every process the job has ever
 * contained, live or dead — monotonically increasing, which is exactly what an
 * idle-detection poll loop needs (compare successive readings; no change since the
 * last poll means nothing in the tree did any CPU work in that interval). Returns
 * -1 on failure (treated as "idle detection unavailable" by callers). */
long long totalCpuTime100ns(HANDLE job) {
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION info{};
  if (!::QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &info, sizeof(info), nullptr)) {
    return -1;
  }
  auto toInt = [](const LARGE_INTEGER& li) { return (static_cast<long long>(li.HighPart) << 32) | li.LowPart; };
  return toInt(info.TotalUserTime) + toInt(info.TotalKernelTime);
}

/** Spawns cmdLine suspended, assigns it to a fresh Job Object, resumes it, streams
 * combined stdout+stderr to onChunk from a background reader thread (mirroring
 * DockerClient::attachAndStream's own thread-plus-callback shape, so
 * native_build.cpp can feed the exact same marker-line parser build.cpp already
 * has for the Docker path), and blocks the calling thread inside `waitLoop` — which
 * owns the actual timeout/idle policy and returns true if it had to kill the
 * process, false if it exited on its own. */
int runWithJobObject(const std::string& cmdLine, const std::string& workingDir,
                      const std::vector<std::string>& envBlock,
                      const std::function<void(const char*, size_t)>& onChunk,
                      const std::function<bool(HANDLE job, HANDLE process)>& waitLoop) {
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE readPipe = nullptr;
  HANDLE writePipe = nullptr;
  if (!::CreatePipe(&readPipe, &writePipe, &sa, 0)) {
    throw std::runtime_error("Could not create a pipe for the child process's output");
  }
  ::SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOA si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = writePipe;
  si.hStdError = writePipe;
  si.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

  std::string env = buildEnvironmentBlock(envBlock);
  std::vector<char> cmdLineBuf(cmdLine.begin(), cmdLine.end());
  cmdLineBuf.push_back('\0');

  PROCESS_INFORMATION pi{};
  // CREATE_SUSPENDED: the process must not run (and potentially spawn its own
  // children, which would then escape the job) before AssignProcessToJobObject
  // below has actually completed.
  BOOL ok = ::CreateProcessA(nullptr, cmdLineBuf.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW | CREATE_SUSPENDED, env.data(),
                              workingDir.empty() ? nullptr : workingDir.c_str(), &si, &pi);
  ::CloseHandle(writePipe);
  if (!ok) {
    ::CloseHandle(readPipe);
    throw std::runtime_error("Could not start process: " + cmdLine);
  }

  JobHandle jh;
  // Best-effort: if the Job Object couldn't be created (jh.job is null, extremely
  // unlikely on any real Windows install), the process still runs — timeouts fall
  // back to TerminateProcess on just the direct child inside waitLoop below,
  // rather than the whole tree.
  if (jh.job) ::AssignProcessToJobObject(jh.job, pi.hProcess);
  ::ResumeThread(pi.hThread);
  ::CloseHandle(pi.hThread);

  std::thread reader([&]() {
    char buf[4096];
    DWORD n = 0;
    while (::ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
      onChunk(buf, static_cast<size_t>(n));
    }
  });

  bool killed = waitLoop(jh.job, pi.hProcess);

  reader.join();
  ::CloseHandle(readPipe);

  DWORD exitCode = 1;
  ::GetExitCodeProcess(pi.hProcess, &exitCode);
  ::CloseHandle(pi.hProcess);

  return killed ? kProcessTimeoutExitCode : static_cast<int>(exitCode);
}

void killProcessTree(HANDLE job, HANDLE process) {
  if (job) {
    ::TerminateJobObject(job, static_cast<UINT>(kProcessTimeoutExitCode));
  } else {
    ::TerminateProcess(process, static_cast<UINT>(kProcessTimeoutExitCode));
  }
  ::WaitForSingleObject(process, INFINITE);
}

}  // namespace

int runProcessWithTimeout(const std::string& cmdLine, const std::string& workingDir,
                           const std::vector<std::string>& envBlock, int timeoutSeconds,
                           const std::function<void(const char*, size_t)>& onChunk) {
  return runWithJobObject(cmdLine, workingDir, envBlock, onChunk, [&](HANDLE job, HANDLE process) {
    DWORD result = ::WaitForSingleObject(process, static_cast<DWORD>(timeoutSeconds) * 1000);
    if (result != WAIT_TIMEOUT) return false;
    killProcessTree(job, process);
    return true;
  });
}

int runProcessWithIdleTimeout(const std::string& cmdLine, const std::string& workingDir,
                               const std::vector<std::string>& envBlock, int idleSeconds, int maxSeconds,
                               const std::function<void(const char*, size_t)>& onChunk) {
  return runWithJobObject(cmdLine, workingDir, envBlock, onChunk, [&](HANDLE job, HANDLE process) {
    constexpr DWORD kPollMs = 5000;
    auto start = std::chrono::steady_clock::now();
    auto lastProgress = start;
    long long lastCpu = job ? totalCpuTime100ns(job) : -1;

    for (;;) {
      DWORD result = ::WaitForSingleObject(process, kPollMs);
      if (result == WAIT_OBJECT_0) return false;

      auto now = std::chrono::steady_clock::now();
      if (std::chrono::duration<double>(now - start).count() >= maxSeconds) {
        killProcessTree(job, process);
        return true;
      }

      if (job) {
        long long cpu = totalCpuTime100ns(job);
        if (cpu > lastCpu) {
          lastCpu = cpu;
          lastProgress = now;
        }
        if (std::chrono::duration<double>(now - lastProgress).count() >= idleSeconds) {
          killProcessTree(job, process);
          return true;
        }
      }
      // job == nullptr: Job Object unavailable, so idle detection is skipped —
      // only the maxSeconds hard ceiling above still applies.
    }
  });
}

}  // namespace ebl
