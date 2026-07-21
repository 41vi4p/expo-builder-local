#pragma once
// High-level Docker Engine API operations built on top of HttpClient + Json +
// the tar writer. Mirrors what orchestrator/src/docker/runner.ts does for the
// GUI/orchestrator, but talking to the daemon directly instead of via dockerode.
#include <functional>
#include <string>

#include "http_client.hpp"

namespace ebl {

struct KeystoreConfig {
  std::string hostPath;
  std::string filename;
  std::string storePassword;
  std::string keyAlias;
  std::string keyPassword;
};

struct BuildParams {
  std::string appPath;      // absolute host path to the Expo project root
  std::string artifactType; // apk | aab
  std::string profile;
  std::string engine;       // auto | gradle | eas
  std::string signingMode;  // debug | release
  std::string expoToken;    // optional
  bool hasKeystore = false;
  KeystoreConfig keystore;
};

class DockerClient {
public:
  explicit DockerClient(std::string socketPath);

  bool imageExists(const std::string& tag);

  /** Tars `contextDir` and POSTs it to /build, invoking onLog for each line of
   * build output. Throws if the daemon reports an error. */
  void buildImage(const std::string& contextDir, const std::string& tag,
                   const std::function<void(const std::string&)>& onLog);

  void ensureVolume(const std::string& name);

  std::string createContainer(const BuildParams& params, const std::string& runnerImage,
                               const std::string& gradleCacheVolume, const std::string& npmCacheVolume,
                               unsigned int buildUid, unsigned int buildGid);
  void startContainer(const std::string& id);

  /** Streams the container's combined stdout/stderr (Tty:true, so it's a raw,
   * unmultiplexed byte stream) — onChunk fires as bytes arrive. */
  void attachAndStream(const std::string& id, const std::function<void(const char*, size_t)>& onChunk);

  /** Blocks until the container exits; returns its exit code. */
  int waitContainer(const std::string& id);

  void removeContainer(const std::string& id);

private:
  HttpClient http_;
};

}  // namespace ebl
