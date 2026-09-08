#include "native_build.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "json.hpp"
#include "native_process.hpp"
#include "runner_context.hpp"

namespace fs = std::filesystem;

namespace ebl {

namespace {

// Same defaults as docker/runner/build-entrypoint.sh's own env-var defaults.
constexpr int kInstallTimeoutSeconds = 300;
constexpr int kInstallFallbackTimeoutSeconds = 600;
constexpr int kPrebuildTimeoutSeconds = 300;
constexpr int kEasBuildTimeoutSeconds = 7200;
constexpr int kEasBuildIdleTimeoutSeconds = 600;
constexpr int kGradleTimeoutSeconds = 7200;
constexpr int kGradleIdleTimeoutSeconds = 600;
constexpr long long kMinFreeDiskMb = 2048;

void emit(const std::function<void(const char*, size_t)>& onChunk, const std::string& line) {
  std::string withNewline = line + "\n";
  onChunk(withNewline.data(), withNewline.size());
}

void phase(const std::function<void(const char*, size_t)>& onChunk, const std::string& id, const std::string& label) {
  emit(onChunk, "@@PHASE:" + id + ":" + label);
}

void progressMarker(const std::function<void(const char*, size_t)>& onChunk, int pct) {
  emit(onChunk, "@@PROGRESS:" + std::to_string(pct));
}

[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }

std::string readFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return "";
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

void writeFile(const fs::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("Could not write " + path.string());
  out << content;
}

void ensureNpmrc(const fs::path& appPath) {
  fs::path p = appPath / ".npmrc";
  if (fs::exists(p)) return;
  writeFile(p, "legacy-peer-deps=true\n");
}

// Same idempotent-append idea as commands/build.cpp's own ensureGitignored (not
// exported from that translation unit, so duplicated here rather than restructuring
// build.cpp's anonymous-namespace helpers into a shared header for one call site).
void ensureGitignoredEblBuilds(const fs::path& appPath) {
  fs::path p = appPath / ".gitignore";
  std::string existing = readFile(p);
  std::istringstream lines(existing);
  std::string line;
  while (std::getline(lines, line)) {
    if (line == "ebl_builds/") return;
  }
  std::ofstream out(p, std::ios::app);
  if (!existing.empty() && existing.back() != '\n') out << "\n";
  out << "ebl_builds/\n";
}

long long freeDiskMb(const std::string& path) {
  ULARGE_INTEGER freeBytesAvailable{};
  if (!::GetDiskFreeSpaceExA(path.c_str(), &freeBytesAvailable, nullptr, nullptr)) return -1;
  return static_cast<long long>(freeBytesAvailable.QuadPart / (1024 * 1024));
}

void checkDiskSpace(const std::string& path, const std::string& label) {
  long long free = freeDiskMb(path);
  // A negative result means the check itself failed (e.g. a UNC path
  // GetDiskFreeSpaceExA doesn't like) - not treated as a hard failure, since it's a
  // defensive pre-flight check, not something the build itself depends on.
  if (free >= 0 && free < kMinFreeDiskMb) {
    fail("Less than " + std::to_string(kMinFreeDiskMb) + "MB free on " + label + " (" + std::to_string(free) +
         "MB available) - free up space and try again.");
  }
}

/** Mirrors build-entrypoint.sh's `trap cleanup EXIT` - always removes the Gradle
 * path's temp signing files (safe: `expo prebuild --clean` regenerates android/
 * from scratch on every gradle-engine run, so anything found there afterward is
 * always this engine's own output, never hand-maintained user content), removes
 * credentials.json only if this run wrote it, and restores eas.json from its
 * pre-signing backup if one was taken. lockFilePath is only set once this run has
 * actually created it (see runNativeBuild) - never removes another run's lock. */
struct Cleanup {
  fs::path androidDir;
  fs::path credentialsJsonPath;
  bool credentialsJsonWritten = false;
  fs::path easJsonPath;
  fs::path easJsonBackup;
  fs::path lockFilePath;

  ~Cleanup() {
    std::error_code ec;
    if (!androidDir.empty()) {
      fs::remove(androidDir / "keystore.properties", ec);
      fs::remove(androidDir / "app" / "release.keystore", ec);
    }
    if (credentialsJsonWritten && !credentialsJsonPath.empty()) {
      fs::remove(credentialsJsonPath, ec);
    }
    if (!easJsonBackup.empty() && fs::exists(easJsonBackup)) {
      fs::rename(easJsonBackup, easJsonPath, ec);
    }
    if (!lockFilePath.empty()) {
      fs::remove(lockFilePath, ec);
    }
  }
};

std::string nodeExe(const NativeToolchainConfig& t) { return t.nodeHome + "\\node.exe"; }
std::string npxCmd(const NativeToolchainConfig& t) { return t.nodeHome + "\\npx.cmd"; }
std::string npmCmd(const NativeToolchainConfig& t) { return t.nodeHome + "\\npm.cmd"; }
std::string easCmd(const NativeToolchainConfig& t) { return t.nodeHome + "\\node-tools\\eas.cmd"; }

std::vector<std::string> baseEnv(const NativeToolchainConfig& t) {
  std::string path = t.jdkHome + "\\bin;" + t.androidSdkRoot + "\\cmdline-tools\\latest\\bin;" + t.androidSdkRoot +
                      "\\platform-tools;" + t.nodeHome + ";" + t.nodeHome + "\\node-tools;";
  if (const char* existing = std::getenv("PATH")) path += existing;
  return {
      "JAVA_HOME=" + t.jdkHome,
      "ANDROID_HOME=" + t.androidSdkRoot,
      "ANDROID_SDK_ROOT=" + t.androidSdkRoot,
      "PATH=" + path,
  };
}

// CreateProcess takes one command-line string, not argv - same Windows argv
// quoting rules as metrics.cpp's quoteWindowsArg (duplicated locally rather than
// exported from that file for one shared 15-line helper).
std::string quoteArg(const std::string& a) {
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

std::string buildCmdLine(const std::vector<std::string>& args) {
  std::string out;
  for (size_t i = 0; i < args.size(); i++) {
    if (i) out += ' ';
    out += quoteArg(args[i]);
  }
  return out;
}

}  // namespace

int runNativeBuild(const BuildParams& params, const NativeToolchainConfig& toolchain,
                    const std::function<void(const char*, size_t)>& onChunk) {
  time_t startedAt = time(nullptr);
  fs::path appPath(params.appPath);

  if (toolchain.jdkHome.empty() || toolchain.androidSdkRoot.empty() || toolchain.nodeHome.empty()) {
    emit(onChunk, "@@ERROR:Native toolchain isn't provisioned yet - run `ebl setup --runtime native` first.");
    return 1;
  }

  Cleanup cleanup;
  cleanup.androidDir = appPath / "android";
  cleanup.credentialsJsonPath = appPath / "credentials.json";
  cleanup.easJsonPath = appPath / "eas.json";
  fs::path lockDir = appPath / "ebl_builds";
  fs::path lockFilePath = lockDir / ".build.lock";

  try {
    if (!fs::exists(appPath)) fail("Project directory " + params.appPath + " not found");

    // Concurrency dedup - the direct analog of Docker's com.expo-builder-local.app-path
    // label + findRunningBuildContainerByAppPath, since there's no container/label
    // mechanism to reuse here. Only marked for cleanup *after* this run actually
    // creates it, so a "someone else is already building" failure never deletes
    // that other run's lock file out from under it.
    fs::create_directories(lockDir);
    if (fs::exists(lockFilePath)) {
      fail("A build for " + params.appPath + " is already running (lock file: " + lockFilePath.string() +
           "). If that's stale (a previous run crashed), delete it and retry.");
    }
    writeFile(lockFilePath, std::to_string(static_cast<unsigned long>(::GetCurrentProcessId())));
    cleanup.lockFilePath = lockFilePath;

    checkDiskSpace(params.appPath, "the project directory");
    checkDiskSpace(toolchain.nodeHome, "the toolchain volume");

    std::vector<std::string> env = baseEnv(toolchain);
    if (!params.expoToken.empty()) env.push_back("EXPO_TOKEN=" + params.expoToken);

    auto runStep = [&](const std::vector<std::string>& args, int timeoutSeconds, const char* stallMessage,
                        const char* failMessage) {
      int exitCode = runProcessWithTimeout(buildCmdLine(args), params.appPath, env, timeoutSeconds, onChunk);
      if (exitCode == kProcessTimeoutExitCode) fail(stallMessage);
      if (exitCode != 0) fail(std::string(failMessage) + " (exit " + std::to_string(exitCode) + ")");
    };
    auto runStepIdle = [&](const std::vector<std::string>& args, int idleSeconds, int maxSeconds,
                            const char* stallMessage, const char* failMessage) {
      int exitCode = runProcessWithIdleTimeout(buildCmdLine(args), params.appPath, env, idleSeconds, maxSeconds, onChunk);
      if (exitCode == kProcessTimeoutExitCode) fail(stallMessage);
      if (exitCode != 0) fail(std::string(failMessage) + " (exit " + std::to_string(exitCode) + ")");
    };

    // --- setup ---
    phase(onChunk, "setup", "Preparing project");
    ensureNpmrc(appPath);
    ensureGitignoredEblBuilds(appPath);
    progressMarker(onChunk, 100);

    // --- install ---
    phase(onChunk, "install", "Installing dependencies");
    if (fs::exists(appPath / "package-lock.json")) {
      int exitCode = runProcessWithTimeout(buildCmdLine({npmCmd(toolchain), "ci", "--no-audit", "--no-fund"}),
                                            params.appPath, env, kInstallTimeoutSeconds, onChunk);
      if (exitCode != 0) {
        if (exitCode == kProcessTimeoutExitCode) {
          emit(onChunk, "npm ci stalled/timed out - falling back to npm install (lockfile may have drifted)");
        }
        runStep({npmCmd(toolchain), "install", "--no-audit", "--no-fund", "--legacy-peer-deps"},
                 kInstallFallbackTimeoutSeconds, "npm install stalled or exceeded its timeout", "npm install failed");
      }
    } else {
      runStep({npmCmd(toolchain), "install", "--no-audit", "--no-fund", "--legacy-peer-deps"},
               kInstallFallbackTimeoutSeconds, "npm install stalled or exceeded its timeout", "npm install failed");
    }
    progressMarker(onChunk, 100);

    // --- engine resolution (same rule as build-entrypoint.sh) ---
    std::string resolvedEngine = params.engine;
    if (resolvedEngine == "auto") {
      resolvedEngine = (!params.expoToken.empty() && fs::exists(appPath / "eas.json")) ? "eas" : "gradle";
    }
    emit(onChunk, "@@ENGINE:" + resolvedEngine);

    bool releaseSigning = params.signingMode == "release" && params.hasKeystore;
    std::string keyPassword = params.keystore.keyPassword.empty() ? params.keystore.storePassword
                                                                    : params.keystore.keyPassword;
    std::string artifactPath;

    if (resolvedEngine == "eas") {
      if (params.expoToken.empty()) fail("ENGINE=eas requires an Expo access token");

      if (releaseSigning) {
        phase(onChunk, "signing", "Configuring release signing (EAS local credentials)");
        cleanup.easJsonBackup = appPath / "eas.json.ebl-backup";
        std::error_code ec;
        fs::copy_file(cleanup.easJsonPath, cleanup.easJsonBackup, fs::copy_options::overwrite_existing, ec);
        cleanup.credentialsJsonWritten = true;  // set before the call, same as build-entrypoint.sh
        runStep({nodeExe(toolchain), resolveNativeScriptsDir() + "\\write-eas-credentials.js", "--projectDir",
                 params.appPath, "--profile", params.profile, "--keystore", params.keystore.hostPath,
                 "--storePassword", params.keystore.storePassword, "--keyAlias", params.keystore.keyAlias,
                 "--keyPassword", keyPassword},
                60, "write-eas-credentials.js stalled", "Failed to prepare local EAS credentials");
      }

      phase(onChunk, "eas", "Building with EAS (local)");
      fs::path scratchDir = fs::temp_directory_path() / "ebl-scratch";
      fs::create_directories(scratchDir);
      artifactPath = (scratchDir / ("eas-output." + params.artifactType)).string();
      runStepIdle({easCmd(toolchain), "build", "--local", "--non-interactive", "--platform", "android", "--profile",
                   params.profile, "--output", artifactPath},
                  kEasBuildIdleTimeoutSeconds, kEasBuildTimeoutSeconds,
                  "eas build --local stalled - no CPU activity for a while (or exceeded the hard time ceiling)",
                  "eas build --local failed - see the eas-cli output above for the actual error");
    } else {
      phase(onChunk, "prebuild", "Generating native Android project");
      runStep({npxCmd(toolchain), "expo", "prebuild", "--platform", "android", "--clean", "--non-interactive"},
               kPrebuildTimeoutSeconds, "expo prebuild stalled or exceeded its timeout", "expo prebuild failed");
      progressMarker(onChunk, 100);

      if (releaseSigning) {
        phase(onChunk, "signing", "Configuring release signing");
        runStep({nodeExe(toolchain), resolveNativeScriptsDir() + "\\patch-android-signing.js", "--androidDir",
                 cleanup.androidDir.string(), "--keystore", params.keystore.hostPath, "--storePassword",
                 params.keystore.storePassword, "--keyAlias", params.keystore.keyAlias, "--keyPassword",
                 keyPassword},
                60, "patch-android-signing.js stalled",
                "Failed to configure release signing - check android/app/build.gradle manually");
      }

      phase(onChunk, "gradle", "Compiling (Gradle)");
      std::string gradleTask = params.artifactType == "aab" ? "bundleRelease" : "assembleRelease";
      fs::path outDir = cleanup.androidDir / "app" / "build" / "outputs" /
                         (params.artifactType == "aab" ? "bundle" : "apk") / "release";
      runStepIdle({(cleanup.androidDir / "gradlew.bat").string(), gradleTask, "--console=plain", "--no-daemon"},
                  kGradleIdleTimeoutSeconds, kGradleTimeoutSeconds,
                  "gradlew stalled - no CPU activity for a while (or exceeded the hard time ceiling)",
                  "gradlew failed");

      std::string found;
      std::error_code ec;
      std::string wantExt = "." + params.artifactType;
      for (const auto& entry : fs::directory_iterator(outDir, ec)) {
        if (entry.path().extension() == wantExt) {
          found = entry.path().string();
          break;
        }
      }
      if (found.empty()) {
        fail("Build reported success but no " + params.artifactType + " was found under " + outDir.string());
      }
      artifactPath = found;
    }

    // --- collect (same v<version>-build<n>/ convention as build-entrypoint.sh) ---
    phase(onChunk, "collect", "Collecting artifact");
    Json pkg = Json::parse(readFile(appPath / "package.json"));
    std::string appName = pkg.get("name").asString("app");
    std::string appVersion = pkg.get("version").asString("0.0.0");

    fs::path counterFile = lockDir / ".build-counter";
    long long prevBuildNumber = 0;
    if (fs::exists(counterFile)) {
      try {
        prevBuildNumber = std::stoll(readFile(counterFile));
      } catch (...) {
        prevBuildNumber = 0;
      }
    }
    long long buildNumber = prevBuildNumber + 1;
    writeFile(counterFile, std::to_string(buildNumber));

    fs::path buildDir = lockDir / ("v" + appVersion + "-build" + std::to_string(buildNumber));
    fs::create_directories(buildDir);
    fs::path finalPath = buildDir / (appName + "-" + params.profile + "." + params.artifactType);
    std::error_code copyEc;
    fs::copy_file(artifactPath, finalPath, fs::copy_options::overwrite_existing, copyEc);
    if (copyEc) fail("Could not copy the built artifact into ebl_builds/: " + copyEc.message());
    progressMarker(onChunk, 100);
    emit(onChunk, "@@BUILD_NUMBER:" + std::to_string(buildNumber));
    emit(onChunk, "@@ARTIFACT:" + finalPath.string());

    long durationSeconds = static_cast<long>(time(nullptr) - startedAt);
    emit(onChunk, "@@DURATION:" + std::to_string(durationSeconds));
    phase(onChunk, "done", "Build complete");
    return 0;
  } catch (const std::exception& e) {
    emit(onChunk, "@@ERROR:" + std::string(e.what()));
    return 1;
  }
}

}  // namespace ebl
