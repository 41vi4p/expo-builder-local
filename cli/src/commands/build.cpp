#include "build.hpp"

#include <curl/curl.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#include "../color.hpp"
#include "../config_store.hpp"
#include "../detect.hpp"
#include "../docker_client.hpp"
#include "../metrics.hpp"
#include "../runner_context.hpp"

namespace fs = std::filesystem;
using ebl::BuildParams;
using ebl::DockerClient;

namespace ebl::commands {

namespace {

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
  -e, --engine <engine>          auto, gradle, or eas (default: auto)
      --release                 Sign with a real keystore instead of the debug keystore
      --keystore <path>          Path to a .jks/.keystore file (required with --release)
      --store-password <pw>      Keystore password (or set EXPO_BUILDER_STORE_PASSWORD)
      --key-alias <alias>        Key alias (required with --release)
      --key-password <pw>        Key password (or set EXPO_BUILDER_KEY_PASSWORD;
                                  defaults to the store password)
      --expo-token <token>       Expo access token, for the eas engine (or set EXPO_TOKEN,
                                  or the token saved by `ebl config`)
      --runner-image <tag>       Runner image tag (default: from `ebl config` if set,
                                  else expo-builder-local-runner:latest)
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
  std::string engine = "auto";
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
    docker.pullImage(tag, [](const std::string& line) { std::cout << line << std::flush; });
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

  // `ebl config` may have saved a Docker Hub namespace and/or an Expo token — use
  // them as defaults, but any explicit flag/env var still wins.
  auto savedConfig = ebl::loadConfig();
  std::string runnerImage = opts.runnerImage.value_or(
      savedConfig ? savedConfig->runnerImage() : "expo-builder-local-runner:latest");

  BuildParams params;
  params.appPath = appPath.string();
  params.artifactType = artifact;
  params.profile = profile;
  params.engine = opts.engine;
  params.signingMode = opts.release ? "release" : "debug";
  params.expoToken = opts.expoToken.value_or(
      envOrNullopt("EXPO_TOKEN").value_or(savedConfig ? savedConfig->expoToken : ""));
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
    // No SIGINT handling: a Ctrl-C here kills this process but leaves the container
    // running (same trade-off `docker run` itself has without an explicit trap). If
    // you interrupt a build, clean it up with: docker rm -f <container id below>.
    std::cout << ebl::color::dim("Container: " + containerId) << "\n";

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
        else if (line.rfind("@@ARTIFACT:", 0) == 0) artifactPath = line.substr(11);
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
    if (!attachError.empty()) {
      std::cerr << "\n" << ebl::color::yellow("Warning: log streaming ended early: " + attachError) << "\n";
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
