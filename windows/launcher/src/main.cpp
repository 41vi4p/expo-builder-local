// ebl.exe — thin Windows launcher that forwards `ebl <args>` into WSL2's real
// Linux ebl binary. Docker Desktop's WSL2 integration already exposes a working
// /var/run/docker.sock inside an integrated distro, so the actual Linux `ebl` CLI
// (built from ../../cli, completely unmodified) runs there as-is — this wrapper
// only has to get the invocation across the Windows/WSL boundary: translate the
// cwd (via wsl.exe's own --cd, which accepts Windows paths directly and does its
// own translation) and any absolute Windows-path *arguments* (which wsl.exe does
// NOT auto-translate, since it has no way to know an opaque argument is a path),
// then forward stdio and the exit code. wsl.exe itself already handles console
// attachment/interactivity (hidden password prompts, Ctrl-C) correctly, so this
// wrapper doesn't need to do anything special for those.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cerrno>
#include <cwctype>
#include <iostream>
#include <process.h>
#include <string>
#include <vector>

namespace {

// EBL_WSL_DISTRO overrides which WSL distro to target; unset uses WSL's own
// default distro, which covers the common case of a single installed distro.
std::wstring distroFromEnv() {
  wchar_t buf[256];
  DWORD n = GetEnvironmentVariableW(L"EBL_WSL_DISTRO", buf, 256);
  if (n > 0 && n < 256) return std::wstring(buf, n);
  return L"";
}

// "C:\..." or "C:/..." — a drive letter followed by a colon and a separator.
bool looksLikeWindowsPath(const std::wstring& s) {
  return s.size() >= 3 && iswalpha(static_cast<wint_t>(s[0])) && s[1] == L':' &&
         (s[2] == L'\\' || s[2] == L'/');
}

// "C:\Users\me\App" -> "/mnt/c/Users/me/App" — the same convention Docker
// Desktop's WSL2 integration and wsl.exe itself use for drive mounts.
std::wstring toWslPath(const std::wstring& winPath) {
  std::wstring out = L"/mnt/";
  out += static_cast<wchar_t>(towlower(static_cast<wint_t>(winPath[0])));
  out += winPath.substr(2);  // drop "C:", keep the separator that follows
  for (auto& c : out) {
    if (c == L'\\') c = L'/';
  }
  return out;
}

// MAX_PATH (260 chars) limit, not long-path-aware — matches the traditional
// Windows default; fine unless invoked from a very deeply nested directory.
std::wstring currentDirectory() {
  wchar_t buf[MAX_PATH];
  DWORD n = GetCurrentDirectoryW(MAX_PATH, buf);
  return std::wstring(buf, n);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  std::vector<std::wstring> wslArgs;
  wslArgs.push_back(L"wsl.exe");

  std::wstring distro = distroFromEnv();
  if (!distro.empty()) {
    wslArgs.push_back(L"-d");
    wslArgs.push_back(distro);
  }

  // wsl.exe's --cd accepts a Windows path directly and translates it itself, so
  // relative paths passed to `ebl` (by far the common case — `ebl build .`)
  // resolve correctly with no translation work on our end at all.
  wslArgs.push_back(L"--cd");
  wslArgs.push_back(currentDirectory());

  wslArgs.push_back(L"--");
  wslArgs.push_back(L"ebl");

  for (int i = 1; i < argc; i++) {
    std::wstring arg = argv[i];
    if (looksLikeWindowsPath(arg)) arg = toWslPath(arg);
    wslArgs.push_back(arg);
  }

  std::vector<const wchar_t*> cargs;
  cargs.reserve(wslArgs.size() + 1);
  for (const auto& a : wslArgs) cargs.push_back(a.c_str());
  cargs.push_back(nullptr);

  intptr_t result = _wspawnvp(_P_WAIT, L"wsl.exe", cargs.data());
  if (result == -1) {
    std::wcerr << L"ebl: failed to launch WSL (errno " << errno << L").\n"
               << L"Is WSL2 installed and is 'wsl.exe' on PATH? Re-run the "
               << L"installer, or see "
               << L"https://github.com/41vi4p/expo-builder-local#windows\n";
    return 1;
  }
  return static_cast<int>(result);
}
