#include "build.hpp"

#include <curl/curl.h>
#include <unistd.h>

#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "../color.hpp"
#include "../config_store.hpp"
#include "../detect.hpp"
#include "../docker_client.hpp"
#include "../metrics.hpp"
#include "../prompt.hpp"
#include "../pull_progress.hpp"
#include "../runner_context.hpp"

namespace fs = std::filesystem;
using ebl::BuildParams;
using ebl::DockerClient;

namespace ebl::commands {

namespace {

// Set by the SIGINT/SIGTERM handler below; only async-signal-safe operations
// (setting a sig_atomic_t) happen in the handler itself. A watcher thread polls
// this and does the actual container cleanup on the main flow's behalf.
volatile std::sig_atomic_t g_interruptRequested = 0;

void handleInterruptSignal(int /* signum */) { g_interruptRequested = 1; }

// A project-local, gitignored fallback for the Expo token — for a team where
// different developers/CI machines build the same checkout but don't share
// `ebl config`'s global ~/.config/ebl state (or don't want a token that broad).
constexpr const char* kProjectTokenFilename = ".ebl-token";

std::optional<std::string> readProjectTokenFile(const fs::path& appPath) {
  std::ifstream in(appPath / kProjectTokenFilename, std::ios::binary);
  if (!in) return std::nullopt;
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  while (!content.empty() && (content.back() == '\n' || content.back() == '\r' || content.back() == ' ')) {
    content.pop_back();
  }
  if (content.empty()) return std::nullopt;
  return content;
}

/** Appends `entry` as its own line in <appPath>/.gitignore, unless already present
 * (exact-line match) — creates the file if it doesn't exist yet. */
void ensureGitignored(const fs::path& appPath, const std::string& entry) {
  fs::path gitignorePath = appPath / ".gitignore";
  std::string existing;
  {
    std::ifstream in(gitignorePath, std::ios::binary);
    if (in) existing.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  std::istringstream lines(existing);
  std::string line;
  while (std::getline(lines, line)) {
    if (line == entry) return;
  }
  std::ofstream out(gitignorePath, std::ios::app);
  if (!existing.empty() && existing.back() != '\n') out << "\n";
  out << entry << "\n";
}

void saveProjectTokenFile(const fs::path& appPath, const std::string& token) {
  fs::path tokenPath = appPath / kProjectTokenFilename;
  std::ofstream out(tokenPath, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("Could not write " + tokenPath.string());
  out << token << "\n";
  out.close();
  ::chmod(tokenPath.c_str(), S_IRUSR | S_IWUSR);
  ensureGitignored(appPath, kProjectTokenFilename);
}

void printUsage() {
  std::cout <<
      R"(ebl build [path] [options]

Build a managed Expo project into a signed Android APK/AAB in a disposable Docker
container. Run it from anywhere, pointing at any Expo project root.

Arguments:
  path                     Path to the Expo project root (default: .)

Options:
      --prod                     Shortcut for --artifact aab --profile production
                                  (defaults otherwise: apk / preview)
  -a, --artifact <type>          apk or aab (default: apk, or aab with --prod)
  -p, --profile <name>           eas.json build profile (default: "preview", or
                                  "production" with --prod, or the project's first
                                  declared profile)
  -e, --engine <engine>          auto, gradle, or eas (default: eas)
      --release                 Sign with a real keystore instead of the debug keystore
      --keystore <path>          Path to a .jks/.keystore file (required with --release)
      --store-password <pw>      Keystore password (or set EXPO_BUILDER_STORE_PASSWORD)
      --key-alias <alias>        Key alias (required with --release)
      --key-password <pw>        Key password (or set EXPO_BUILDER_KEY_PASSWORD;
                                  defaults to the store password)
      --expo-token <token>       Expo access token, for the eas engine. Resolved in order:
                                  this flag, EXPO_TOKEN, a .ebl-token file in the project
                                  (gitignored automatically — see below), the per-owner/
                                  default token saved by `ebl config` (auto-selected by the
                                  project's app.json "owner" field). If none of these and
                                  the engine needs one, you'll be prompted interactively,
                                  with the option to save it to .ebl-token for next time.
      --runner-image <tag>       Runner image tag (default: from `ebl config` if set,
                                  else 41vi4p/expo-builder-local-runner:latest)
      --gradle-cache-volume <n>  Docker volume for the Gradle cache
      --npm-cache-volume <n>     Docker volume for the npm cache
      --docker-socket <path>     Docker socket path (default: /var/run/docker.sock)
  -h, --help                     Show this help
)";
}

struct Options {
  std::string path = ".";
  bool prod = false;
  std::optional<std::string> artifact;
  std::optional<std::string> profile;
  std::string engine = "eas";
  bool release = false;
  std::optional<std::string> keystore;
  std::optional<std::string> storePassword;
  std::optional<std::string> keyAlias;
  std::optional<std::string> keyPassword;
  std::optional<std::string> expoToken;
  std::optional<std::string> runnerImage;
  std::string gradleCacheVolume = "expo-builder-local_gradle-cache";
  std::string npmCacheVolume = "expo-builder-local_npm-cache";
  std::string dockerSocket = "/var/run/docker.sock";
};

bool parseArgs(int argc, char** argv, Options& opts, int& exitCode) {
  bool sawPositional = false;
  auto needValue = [&](int& i, const char* flagName) -> std::string {
    if (i + 1 >= argc) {
      std::cerr << ebl::color::red(std::string("Missing value for ") + flagName) << "\n";
      exitCode = 2;
      throw std::runtime_error("usage");
    }
    return argv[++i];
  };

  try {
    for (int i = 0; i < argc; i++) {
      std::string arg = argv[i];
      if (arg == "-h" || arg == "--help") {
        printUsage();
        exitCode = 0;
        return false;
      }
      if (arg == "--prod") { opts.prod = true; continue; }
      if (arg == "-a" || arg == "--artifact") { opts.artifact = needValue(i, "--artifact"); continue; }
      if (arg == "-p" || arg == "--profile") { opts.profile = needValue(i, "--profile"); continue; }
      if (arg == "-e" || arg == "--engine") { opts.engine = needValue(i, "--engine"); continue; }
      if (arg == "--release") { opts.release = true; continue; }
      if (arg == "--keystore") { opts.keystore = needValue(i, "--keystore"); continue; }
      if (arg == "--store-password") { opts.storePassword = needValue(i, "--store-password"); continue; }
      if (arg == "--key-alias") { opts.keyAlias = needValue(i, "--key-alias"); continue; }
      if (arg == "--key-password") { opts.keyPassword = needValue(i, "--key-password"); continue; }
      if (arg == "--expo-token") { opts.expoToken = needValue(i, "--expo-token"); continue; }
      if (arg == "--runner-image") { opts.runnerImage = needValue(i, "--runner-image"); continue; }
      if (arg == "--gradle-cache-volume") { opts.gradleCacheVolume = needValue(i, "--gradle-cache-volume"); continue; }
      if (arg == "--npm-cache-volume") { opts.npmCacheVolume = needValue(i, "--npm-cache-volume"); continue; }
      if (arg == "--docker-socket") { opts.dockerSocket = needValue(i, "--docker-socket"); continue; }
      if (!arg.empty() && arg[0] == '-') {
        std::cerr << ebl::color::red("Unknown option: " + arg) << "\n";
        exitCode = 2;
        return false;
      }
      if (sawPositional) {
        std::cerr << ebl::color::red("Unexpected extra argument: " + arg) << "\n";
        exitCode = 2;
        return false;
      }
      opts.path = arg;
      sawPositional = true;
    }
  } catch (const std::runtime_error&) {
    return false;
  }
  return true;
}

std::optional<std::string> envOrNullopt(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::optional<std::string>(v) : std::nullopt;
}

/** The `@@ARTIFACT:` marker build-entrypoint.sh emits is a path inside the build
 * container (rooted at ebl::kContainerAppDir, e.g. "/work/app/ebl_builds/..."), but
 * the CLI runs natively on the host — translate it back to the real host path
 * (params.appPath, the directory bind-mounted to kContainerAppDir) before touching
 * the filesystem (metrics extraction, printing the artifact path, etc). */
std::string toHostArtifactPath(const std::string& appPath, const std::string& containerPath) {
  std::string prefix = std::string(ebl::kContainerAppDir) + "/";
  if (containerPath.rfind(prefix, 0) != 0) return containerPath;
  return (fs::path(appPath) / containerPath.substr(prefix.size())).string();
}

std::string formatBytes(uint64_t bytes) {
  char buf[64];
  if (bytes < 1024ULL * 1024) {
    std::snprintf(buf, sizeof(buf), "%.0f KB", bytes / 1024.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
  }
  return buf;
}

std::string formatDuration(long seconds) {
  long m = seconds / 60;
  long s = seconds % 60;
  char buf[32];
  if (m > 0) {
    std::snprintf(buf, sizeof(buf), "%ldm %llds", m, static_cast<long long>(s));
  } else {
    std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(s));
  }
  return buf;
}

/** Tries, in order: use the image if already present locally; pull it (works once
 * it's published to Docker Hub, for anyone — not just users who ran `ebl setup`);
 * fall back to building it from the bundled context (fully offline-capable). */
void ensureRunnerImage(DockerClient& docker, const std::string& tag) {
  if (docker.imageExists(tag)) return;

  std::cout << ebl::color::yellow("Runner image \"" + tag + "\" not found locally — trying to pull it...") << "\n";
  try {
    ebl::PullProgressRenderer progress;
    docker.pullImage(tag, [&progress](const std::string& id, const std::string& status, const std::string& p) {
      progress.onEvent(id, status, p);
    });
    std::cout << ebl::color::green("Pulled \"" + tag + "\".") << "\n";
    return;
  } catch (const std::exception& e) {
    std::cout << ebl::color::dim(std::string("Pull failed (") + e.what() + ") — building it locally instead...")
              << "\n";
  }

  std::cout << ebl::color::yellow("Building \"" + tag + "\" now (one-time, ~10-20 minutes)...") << "\n";
  std::string contextDir = ebl::resolveRunnerContextDir();
  docker.buildImage(contextDir, tag, [](const std::string& line) { std::cout << line << std::flush; });
  std::cout << ebl::color::green("Runner image \"" + tag + "\" built.") << "\n";
}

}  // namespace

void printBuildUsage() { printUsage(); }

int runBuild(int argc, char** argv) {
  Options opts;
  int exitCode = 0;
  if (!parseArgs(argc, argv, opts, exitCode)) return exitCode;

  // --prod is sugar for the production defaults, but explicit --artifact/--profile
  // (if the user passed them too) always win.
  std::string artifact = opts.artifact.value_or(opts.prod ? "aab" : "apk");

  fs::path appPath = fs::absolute(opts.path).lexically_normal();
  if (!fs::exists(appPath) || !fs::is_directory(appPath)) {
    std::cerr << ebl::color::red("Not a directory: " + appPath.string()) << "\n";
    return 2;
  }

  ebl::ExpoProjectInfo project = ebl::detectExpoProject(appPath.string());
  if (!project.isExpoProject) {
    std::cerr << ebl::color::red(appPath.string() + " doesn't look like an Expo project: " + project.reason) << "\n";
    return 2;
  }

  if (artifact != "apk" && artifact != "aab") {
    std::cerr << ebl::color::red("--artifact must be \"apk\" or \"aab\", got \"" + artifact + "\"") << "\n";
    return 2;
  }
  if (opts.engine != "auto" && opts.engine != "gradle" && opts.engine != "eas") {
    std::cerr << ebl::color::red("--engine must be \"auto\", \"gradle\", or \"eas\", got \"" + opts.engine + "\"") << "\n";
    return 2;
  }
  if (opts.release && !opts.keystore) {
    std::cerr << ebl::color::red("--release requires --keystore <path>") << "\n";
    return 2;
  }
  if (opts.keystore && !fs::exists(fs::path(*opts.keystore))) {
    std::cerr << ebl::color::red("Keystore not found: " + fs::absolute(*opts.keystore).string()) << "\n";
    return 2;
  }

  std::string profile;
  if (opts.profile) {
    profile = *opts.profile;
  } else if (opts.prod) {
    profile = "production";
  } else {
    bool hasPreview = false;
    for (const auto& p : project.easProfiles) {
      if (p == "preview") hasPreview = true;
    }
    profile = hasPreview || project.easProfiles.empty() ? "preview" : project.easProfiles.front();
  }

  // `ebl config` may have saved a default/per-owner Expo token — use it as a
  // default, but any explicit flag/env var still wins. Falling back to a fresh
  // EblConfig{} (not a hardcoded literal) when no config was ever saved keeps this
  // in sync with the real default runner image defined in config_store.hpp.
  auto savedConfig = ebl::loadConfig();
  std::string runnerImage =
      opts.runnerImage.value_or(savedConfig.value_or(ebl::EblConfig{}).runnerImage());

  BuildParams params;
  params.appPath = appPath.string();
  params.artifactType = artifact;
  params.profile = profile;
  params.engine = opts.engine;
  params.signingMode = opts.release ? "release" : "debug";
  if (opts.expoToken) {
    params.expoToken = *opts.expoToken;
  } else if (auto envToken = envOrNullopt("EXPO_TOKEN")) {
    params.expoToken = *envToken;
  } else if (auto fileToken = readProjectTokenFile(appPath)) {
    params.expoToken = *fileToken;
    std::cout << ebl::color::dim("Using Expo token from " + std::string(kProjectTokenFilename) + ".") << "\n";
  } else if (savedConfig) {
    params.expoToken = savedConfig->expoTokenFor(project.owner);
    bool viaOwner = !project.owner.empty() &&
                     std::any_of(savedConfig->expoTokensByOwner.begin(), savedConfig->expoTokensByOwner.end(),
                                 [&](const ebl::ExpoTokenEntry& e) { return e.owner == project.owner; });
    if (viaOwner) {
      std::cout << ebl::color::dim("Using saved Expo token for owner \"" + project.owner + "\".") << "\n";
    }
  }

  // Only the "eas" engine actually needs a token; "auto" needs one exactly when it
  // would resolve to eas (same eas.json check as build-entrypoint.sh's own auto
  // resolution — see v0.6.6), and "gradle" never does. Prompting here (rather than
  // just letting the container fail later with "ENGINE=eas requires an
  // EXPO_TOKEN") means a missing token doesn't cost you the time spent pulling the
  // runner image and starting the container first.
  bool engineNeedsToken = opts.engine == "eas" || (opts.engine == "auto" && fs::exists(appPath / "eas.json"));
  if (params.expoToken.empty() && engineNeedsToken) {
    std::cout << ebl::color::yellow("No Expo token found, but the \"" + opts.engine + "\" engine needs one.") << "\n";
    std::string entered =
        ebl::promptHidden("Expo access token (from https://expo.dev/accounts/[account]/settings/access-tokens)");
    if (entered.empty()) {
      std::cerr << ebl::color::red("No token entered — aborting.") << "\n";
      return 2;
    }
    params.expoToken = entered;

    std::string save = ebl::promptString(
        "Save this token to " + std::string(kProjectTokenFilename) + " in this project for future builds? [Y/n]",
        "Y");
    if (!save.empty() && (save[0] == 'y' || save[0] == 'Y')) {
      try {
        saveProjectTokenFile(appPath, entered);
        std::cout << ebl::color::green("Saved to " + (appPath / kProjectTokenFilename).string() +
                                        " (added to .gitignore).")
                  << "\n";
      } catch (const std::exception& e) {
        std::cerr << ebl::color::red(std::string("Could not save token file: ") + e.what()) << "\n";
      }
    }
  }
  if (opts.release && opts.keystore) {
    params.hasKeystore = true;
    params.keystore.hostPath = fs::absolute(*opts.keystore).string();
    params.keystore.filename = fs::path(*opts.keystore).filename().string();
    params.keystore.storePassword = opts.storePassword.value_or(envOrNullopt("EXPO_BUILDER_STORE_PASSWORD").value_or(""));
    params.keystore.keyAlias = opts.keyAlias.value_or("");
    params.keystore.keyPassword = opts.keyPassword.value_or(envOrNullopt("EXPO_BUILDER_KEY_PASSWORD").value_or(""));
  }

  curl_global_init(CURL_GLOBAL_DEFAULT);
  int result = 1;
  try {
    DockerClient docker(opts.dockerSocket);

    // Two `ebl build`s of the same project at once share the same npm/Gradle cache
    // volumes and end up contending on npm's own cache lock — neither one makes any
    // real progress rather than failing cleanly. Fail fast instead of launching a
    // second container that would just wedge alongside the first. Thrown (not a
    // direct return) so this still goes through curl_global_cleanup() below.
    if (auto existing = docker.findRunningBuildContainerByAppPath(appPath.string())) {
      throw std::runtime_error("A build for " + appPath.string() + " is already running (container " +
                                existing->substr(0, 12) +
                                "). Wait for it to finish, or stop it with: docker stop " + *existing);
    }

    ensureRunnerImage(docker, runnerImage);
    docker.ensureVolume(opts.gradleCacheVolume);
    docker.ensureVolume(opts.npmCacheVolume);

    std::cout << "\n" << ebl::color::bold("Building " + ebl::color::cyan(appPath.string())) << "\n";
    std::cout << ebl::color::dim("  profile=" + profile + " artifact=" + artifact + " engine=" + opts.engine +
                                  " signing=" + params.signingMode)
              << "\n\n";

    std::string containerId =
        docker.createContainer(params, runnerImage, opts.gradleCacheVolume, opts.npmCacheVolume,
                                static_cast<unsigned int>(getuid()), static_cast<unsigned int>(getgid()));
    std::cout << ebl::color::dim("Container: " + containerId) << "\n";
    std::cout << ebl::color::dim("Press Ctrl-C to cancel — the container will be stopped and removed.") << "\n";

    // Ctrl-C (SIGINT) or a `kill` (SIGTERM) sets g_interruptRequested; this watcher
    // thread notices it and force-removes the container, which is what unblocks the
    // main thread's waitContainer() below (it's a plain blocking libcurl call with no
    // other cancellation point). Without this, interrupting `ebl build` would kill
    // this process but leave the container running indefinitely — which is exactly
    // how three orphaned, mutually-wedging build containers piled up in practice.
    std::signal(SIGINT, handleInterruptSignal);
    std::signal(SIGTERM, handleInterruptSignal);
    std::atomic<bool> buildFinished{false};
    bool wasCancelled = false;
    std::thread cancelWatcher([&]() {
      while (!buildFinished.load()) {
        if (g_interruptRequested) {
          wasCancelled = true;
          std::cerr << "\n" << ebl::color::yellow("Cancelling — stopping and removing the build container...") << "\n";
          try {
            docker.removeContainer(containerId);
          } catch (const std::exception& e) {
            std::cerr << ebl::color::red(std::string("Failed to remove container: ") + e.what()) << "\n";
            std::cerr << ebl::color::dim("Clean it up manually with: docker rm -f " + containerId) << "\n";
          }
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    });

    std::string resolvedEngine;
    std::string artifactPath;
    std::string errorMessage;
    std::string buildNumber;
    std::string residual;

    auto onChunk = [&](const char* data, size_t len) {
      std::cout.write(data, static_cast<std::streamsize>(len));
      std::cout.flush();

      residual.append(data, len);
      size_t pos;
      while ((pos = residual.find_first_of("\r\n")) != std::string::npos) {
        std::string line = residual.substr(0, pos);
        residual.erase(0, pos + 1);
        if (line.rfind("@@ENGINE:", 0) == 0) resolvedEngine = line.substr(9);
        else if (line.rfind("@@ARTIFACT:", 0) == 0) artifactPath = toHostArtifactPath(params.appPath, line.substr(11));
        else if (line.rfind("@@ERROR:", 0) == 0) errorMessage = line.substr(8);
        else if (line.rfind("@@BUILD_NUMBER:", 0) == 0) buildNumber = line.substr(15);
      }
    };

    // attachAndStream blocks (via libcurl) until the container's output stream
    // closes, which only happens once the container exits — so it has to run on its
    // own thread. The main thread then starts the container and waits for it, and
    // joins the attach thread afterward to make sure every last buffered chunk of
    // output has been flushed before we print the summary.
    std::string attachError;
    std::thread attachThread([&]() {
      try {
        docker.attachAndStream(containerId, onChunk);
      } catch (const std::exception& e) {
        attachError = e.what();
      }
    });

    time_t startedAt = time(nullptr);
    docker.startContainer(containerId);
    int exitStatus = docker.waitContainer(containerId);
    long durationSeconds = static_cast<long>(time(nullptr) - startedAt);

    attachThread.join();

    buildFinished.store(true);
    cancelWatcher.join();
    std::signal(SIGINT, SIG_DFL);
    std::signal(SIGTERM, SIG_DFL);

    if (!attachError.empty()) {
      std::cerr << "\n" << ebl::color::yellow("Warning: log streaming ended early: " + attachError) << "\n";
    }

    if (wasCancelled) {
      std::cout << "\n" << ebl::color::yellow(ebl::color::bold("Build cancelled after " + formatDuration(durationSeconds))) << "\n\n";
      result = 130;  // 128 + SIGINT, standard shell convention
      curl_global_cleanup();
      return result;
    }

    docker.removeContainer(containerId);

    if (exitStatus == 0 && !artifactPath.empty()) {
      ebl::ArtifactMetrics metrics = ebl::extractArtifactMetrics(params.appPath, artifactPath);
      std::cout << "\n"
                << ebl::color::green(ebl::color::bold("Build " + (buildNumber.empty() ? "" : "#" + buildNumber + " ") +
                                                       "succeeded in " + formatDuration(durationSeconds)))
                << "\n";
      std::cout << "  " << ebl::color::dim("Artifact:") << "     " << artifactPath << "\n";
      std::cout << "  " << ebl::color::dim("Size:") << "         " << formatBytes(metrics.sizeBytes) << "\n";
      std::cout << "  " << ebl::color::dim("Version:") << "      " << (metrics.versionName.empty() ? "?" : metrics.versionName);
      if (!metrics.versionCode.empty()) std::cout << " (versionCode " << metrics.versionCode << ")";
      std::cout << "\n";
      if (!metrics.applicationId.empty()) std::cout << "  " << ebl::color::dim("Application:") << "  " << metrics.applicationId << "\n";
      std::cout << "  " << ebl::color::dim("Engine:") << "       " << (resolvedEngine.empty() ? opts.engine : resolvedEngine) << "\n";
      if (!metrics.gitCommit.empty()) std::cout << "  " << ebl::color::dim("Git:") << "          " << metrics.gitBranch << "@" << metrics.gitCommit << "\n";
      std::cout << "  " << ebl::color::dim("SHA-256:") << "      " << metrics.sha256 << "\n\n";
      result = 0;
    } else {
      std::cout << "\n" << ebl::color::red(ebl::color::bold("Build failed after " + formatDuration(durationSeconds))) << "\n";
      if (!errorMessage.empty()) std::cout << "  " << errorMessage << "\n";
      else std::cout << "  Build process exited with status " << exitStatus << "\n";
      std::cout << "\n";
      result = 1;
    }
  } catch (const std::exception& e) {
    std::cerr << "\n" << ebl::color::red(std::string(e.what())) << "\n";
    result = 1;
  }

  curl_global_cleanup();
  return result;
}

}  // namespace ebl::commands
