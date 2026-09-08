#include "native_toolchain.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <curl/curl.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

#include "native_process.hpp"

namespace fs = std::filesystem;

namespace ebl {

namespace {

// Same pinned versions as docker/runner/Dockerfile's ARGs, so a native build and a
// Docker build compile against an identical Android toolchain. Bump both together
// if the Dockerfile's ARGs ever change.
constexpr const char* kAndroidCmdlineToolsVersion = "11076708";
constexpr const char* kAndroidPlatform = "android-35";
constexpr const char* kAndroidPlatformMin = "android-34";
constexpr const char* kAndroidBuildTools = "35.0.0";
constexpr const char* kNdkVersion = "27.1.12297006";
constexpr const char* kCmakeVersion = "3.22.1";

// Node LTS is a rolling target in docker/runner/Dockerfile (NodeSource's
// "setup_lts.x" always resolves to whatever's current). A native install has to
// pin something concrete instead — this is a best-effort snapshot of a recent
// Node 22.x LTS point release (matching CLAUDE.md's "Node 22 LTS" baseline) and
// should be bumped by hand periodically; it is not auto-resolved against
// nodejs.org's "latest-lts" listing to avoid the added fragility of parsing a
// directory index for a first version of this provisioner.
constexpr const char* kNodeVersion = "22.11.0";

std::string toolchainRoot() {
  const char* localAppData = std::getenv("LOCALAPPDATA");
  if (!localAppData || !*localAppData) {
    throw std::runtime_error("Could not determine a toolchain install location (%LOCALAPPDATA% is unset)");
  }
  return std::string(localAppData) + "\\ebl\\toolchain";
}

std::string getEnvVar(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

/** Downloads `url` to `destPath` via libcurl (already linked — find_package(CURL
 * REQUIRED) in CMakeLists.txt). Follows redirects (Adoptium/nodejs.org both
 * redirect to a CDN), fails on HTTP error status rather than writing an error page
 * to the file, and verifies TLS normally (no verification is disabled here). */
void downloadFile(const std::string& url, const std::string& destPath) {
  fs::create_directories(fs::path(destPath).parent_path());

  FILE* fp = std::fopen(destPath.c_str(), "wb");
  if (!fp) throw std::runtime_error("Could not open " + destPath + " for writing");

  CURL* curl = curl_easy_init();
  if (!curl) {
    std::fclose(fp);
    throw std::runtime_error("Could not initialize libcurl for downloading " + url);
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "expo-builder-local-ebl");
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 1800L);  // 30 min ceiling for a big SDK/JDK zip on a slow link

  CURLcode res = curl_easy_perform(curl);
  long httpStatus = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
  curl_easy_cleanup(curl);
  std::fclose(fp);

  if (res != CURLE_OK) {
    fs::remove(destPath);
    throw std::runtime_error("Download failed (" + std::string(curl_easy_strerror(res)) + "): " + url);
  }
  if (httpStatus != 0 && httpStatus >= 400) {
    fs::remove(destPath);
    throw std::runtime_error("Download failed (HTTP " + std::to_string(httpStatus) + "): " + url);
  }
}

/** Extracts a zip via PowerShell's Expand-Archive — the same tool
 * windows/install.ps1 already uses for the CLI's own release zip — rather than
 * adding a new C++ zip-library dependency just for this. */
void extractZip(const std::string& zipPath, const std::string& destDir,
                 const std::function<void(const std::string&)>& onLog) {
  fs::create_directories(destDir);
  std::string cmdLine = "powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "
                         "\"Expand-Archive -LiteralPath '" +
                         zipPath + "' -DestinationPath '" + destDir + "' -Force\"";
  int exitCode = runProcessWithTimeout(cmdLine, "", {}, 600, [&](const char* data, size_t len) {
    onLog(std::string(data, len));
  });
  if (exitCode != 0) {
    throw std::runtime_error("Expand-Archive failed (exit " + std::to_string(exitCode) + ") extracting " + zipPath);
  }
}

// --- JDK ---------------------------------------------------------------------

std::string detectExistingJdk17() {
  std::string javaHome = getEnvVar("JAVA_HOME");
  if (!javaHome.empty() && fs::exists(fs::path(javaHome) / "bin" / "javac.exe")) {
    // Best-effort version check only — if javac exists under JAVA_HOME at all,
    // assume it's usable rather than parsing `javac -version`'s stdout (which
    // Java has, at various points, written to stderr instead depending on
    // version — not worth the fragility for a detect-and-reuse heuristic).
    return javaHome;
  }
  return "";
}

std::string ensureJdk(NativeToolchainConfig& toolchain, const std::function<void(const std::string&)>& onLog) {
  if (!toolchain.jdkHome.empty() && fs::exists(fs::path(toolchain.jdkHome) / "bin" / "javac.exe")) {
    onLog("JDK already provisioned at " + toolchain.jdkHome);
    return toolchain.jdkHome;
  }

  if (std::string existing = detectExistingJdk17(); !existing.empty()) {
    onLog("Found an existing JDK at " + existing + " (JAVA_HOME) — reusing it.");
    toolchain.jdkHome = existing;
    toolchain.jdkInstalledByEbl = false;
    return existing;
  }

  onLog("No JDK 17 found — downloading Eclipse Temurin 17 (Adoptium)...");
  std::string root = toolchainRoot();
  std::string zipPath = root + "\\jdk17.zip";
  std::string extractDir = root + "\\jdk17";
  downloadFile("https://api.adoptium.net/v3/binary/latest/17/ga/windows/x64/jdk/hotspot/normal/eclipse", zipPath);
  extractZip(zipPath, extractDir, onLog);
  fs::remove(zipPath);

  // Adoptium's zip contains one top-level "jdk-17.x.x+y" directory.
  std::string jdkHome;
  for (const auto& entry : fs::directory_iterator(extractDir)) {
    if (entry.is_directory() && fs::exists(entry.path() / "bin" / "javac.exe")) {
      jdkHome = entry.path().string();
      break;
    }
  }
  if (jdkHome.empty()) throw std::runtime_error("Extracted JDK zip did not contain a recognizable JDK layout");

  onLog("JDK installed at " + jdkHome);
  toolchain.jdkHome = jdkHome;
  toolchain.jdkInstalledByEbl = true;
  return jdkHome;
}

// --- Android SDK ---------------------------------------------------------------

std::string detectExistingAndroidSdk() {
  for (const char* var : {"ANDROID_HOME", "ANDROID_SDK_ROOT"}) {
    std::string root = getEnvVar(var);
    if (!root.empty() && (fs::exists(fs::path(root) / "platform-tools" / "adb.exe") ||
                           fs::exists(fs::path(root) / "cmdline-tools"))) {
      return root;
    }
  }
  return "";
}

std::string ensureAndroidSdk(NativeToolchainConfig& toolchain, const std::string& jdkHome,
                              const std::function<void(const std::string&)>& onLog) {
  if (!toolchain.androidSdkRoot.empty() && fs::exists(fs::path(toolchain.androidSdkRoot) / "platform-tools")) {
    onLog("Android SDK already provisioned at " + toolchain.androidSdkRoot);
    return toolchain.androidSdkRoot;
  }

  if (std::string existing = detectExistingAndroidSdk(); !existing.empty()) {
    onLog("Found an existing Android SDK at " + existing + " — reusing it (won't touch its packages).");
    toolchain.androidSdkRoot = existing;
    toolchain.androidSdkInstalledByEbl = false;
    return existing;
  }

  onLog("No Android SDK found — downloading the command-line tools...");
  std::string root = toolchainRoot();
  std::string sdkRoot = root + "\\android-sdk";
  std::string zipPath = root + "\\cmdline-tools.zip";
  std::string extractDir = sdkRoot + "\\cmdline-tools-tmp";
  downloadFile("https://dl.google.com/android/repository/commandlinetools-win-" +
                   std::string(kAndroidCmdlineToolsVersion) + "_latest.zip",
               zipPath);
  extractZip(zipPath, extractDir, onLog);
  fs::remove(zipPath);

  // sdkmanager expects <sdkRoot>/cmdline-tools/latest/bin/sdkmanager.bat — the zip
  // extracts a top-level "cmdline-tools/" directory, so move its contents into the
  // "latest" subdirectory sdkmanager itself expects to find its own tools under.
  fs::path latestDir = fs::path(sdkRoot) / "cmdline-tools" / "latest";
  fs::create_directories(latestDir.parent_path());
  fs::rename(fs::path(extractDir) / "cmdline-tools", latestDir);
  fs::remove_all(extractDir);

  std::string sdkmanager = (latestDir / "bin" / "sdkmanager.bat").string();
  std::vector<std::string> env = {"JAVA_HOME=" + jdkHome,
                                   "PATH=" + jdkHome + "\\bin;" + getEnvVar("PATH")};

  onLog("Accepting Android SDK licenses...");
  std::string acceptLicensesCmd =
      "cmd.exe /c \"(for /L %i in (1,1,20) do @echo y) | \"" + sdkmanager + "\" --licenses\"";
  runProcessWithTimeout(acceptLicensesCmd, "", env, 120, [&](const char* d, size_t n) { onLog(std::string(d, n)); });

  onLog("Installing Android SDK packages (platform-tools, platforms " + std::string(kAndroidPlatform) + "/" +
        kAndroidPlatformMin + ", build-tools " + kAndroidBuildTools + ", ndk " + kNdkVersion + ", cmake " +
        kCmakeVersion + ")...");
  std::string installCmd = "\"" + sdkmanager + "\" \"platform-tools\" \"platforms;" + std::string(kAndroidPlatform) +
                            "\" \"platforms;" + kAndroidPlatformMin + "\" \"build-tools;" + kAndroidBuildTools +
                            "\" \"ndk;" + kNdkVersion + "\" \"cmake;" + kCmakeVersion + "\"";
  int exitCode = runProcessWithTimeout(installCmd, "", env, 1800,
                                        [&](const char* d, size_t n) { onLog(std::string(d, n)); });
  if (exitCode != 0) {
    throw std::runtime_error("sdkmanager package install failed (exit " + std::to_string(exitCode) + ")");
  }

  onLog("Android SDK installed at " + sdkRoot);
  toolchain.androidSdkRoot = sdkRoot;
  toolchain.androidSdkInstalledByEbl = true;
  return sdkRoot;
}

// --- Node --------------------------------------------------------------------

std::string detectExistingNode() {
  // A simple PATH-based check: `where node` (no timeout wrapper needed, this is a
  // sub-second lookup) — reusing runProcessWithTimeout with a short ceiling keeps
  // this consistent with every other subprocess call in this file rather than
  // introducing a second ad hoc process-spawning path.
  std::string output;
  int exitCode = runProcessWithTimeout("where node", "", {}, 10,
                                        [&](const char* d, size_t n) { output.append(d, n); });
  if (exitCode != 0 || output.empty()) return "";
  // First line of `where`'s output is the resolved path to node.exe.
  size_t nl = output.find_first_of("\r\n");
  std::string nodeExe = nl == std::string::npos ? output : output.substr(0, nl);
  fs::path nodeDir = fs::path(nodeExe).parent_path();
  return fs::exists(nodeDir / "node.exe") ? nodeDir.string() : "";
}

std::string ensureNode(NativeToolchainConfig& toolchain, const std::function<void(const std::string&)>& onLog) {
  if (!toolchain.nodeHome.empty() && fs::exists(fs::path(toolchain.nodeHome) / "node.exe")) {
    onLog("Node already provisioned at " + toolchain.nodeHome);
    return toolchain.nodeHome;
  }

  if (std::string existing = detectExistingNode(); !existing.empty()) {
    onLog("Found an existing Node install at " + existing + " — reusing it.");
    toolchain.nodeHome = existing;
    toolchain.nodeInstalledByEbl = false;
    return existing;
  }

  onLog("No Node install found — downloading Node " + std::string(kNodeVersion) + "...");
  std::string root = toolchainRoot();
  std::string zipPath = root + "\\node.zip";
  std::string extractDir = root + "\\node-tmp";
  downloadFile("https://nodejs.org/dist/v" + std::string(kNodeVersion) + "/node-v" + kNodeVersion + "-win-x64.zip",
               zipPath);
  extractZip(zipPath, extractDir, onLog);
  fs::remove(zipPath);

  // The zip's single top-level directory (e.g. "node-v22.11.0-win-x64") becomes
  // the final node install dir directly — no separate "tools" subdirectory the
  // way the JDK/Android SDK need, Node's own zip layout is already flat/portable.
  std::string nodeHome = root + "\\node";
  fs::path extracted;
  for (const auto& entry : fs::directory_iterator(extractDir)) {
    if (entry.is_directory()) {
      extracted = entry.path();
      break;
    }
  }
  if (extracted.empty()) throw std::runtime_error("Extracted Node zip did not contain the expected directory");
  if (fs::exists(nodeHome)) fs::remove_all(nodeHome);
  fs::rename(extracted, nodeHome);
  fs::remove_all(extractDir);

  onLog("Installing eas-cli (into its own prefix under this Node install, not the system-wide npm global)...");
  // -g *and* --prefix together is the documented way to get npm's normal
  // global-style layout (a runnable "eas.cmd" shim directly under the prefix
  // dir, not buried in node_modules/.bin) rooted at a custom directory instead
  // of the real global npm prefix — see native_build.cpp's kEasCmdRelativePath.
  std::string npmCmd =
      "\"" + nodeHome + "\\npm.cmd\" install -g eas-cli --prefix \"" + nodeHome + "\\node-tools\"";
  std::vector<std::string> env = {"PATH=" + nodeHome + ";" + getEnvVar("PATH")};
  int exitCode =
      runProcessWithTimeout(npmCmd, "", env, 300, [&](const char* d, size_t n) { onLog(std::string(d, n)); });
  if (exitCode != 0) {
    throw std::runtime_error("npm install eas-cli failed (exit " + std::to_string(exitCode) + ")");
  }

  onLog("Node installed at " + nodeHome);
  toolchain.nodeHome = nodeHome;
  toolchain.nodeInstalledByEbl = true;
  return nodeHome;
}

}  // namespace

void provisionNativeToolchain(NativeToolchainConfig& toolchain, const std::function<void(const std::string&)>& onLog) {
  fs::create_directories(toolchainRoot());
  std::string jdkHome = ensureJdk(toolchain, onLog);
  ensureAndroidSdk(toolchain, jdkHome, onLog);
  ensureNode(toolchain, onLog);
}

}  // namespace ebl
