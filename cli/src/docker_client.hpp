#pragma once
// High-level Docker Engine API operations built on top of HttpClient + Json +
// the tar writer. Mirrors what orchestrator/src/docker/runner.ts does for the
// GUI/orchestrator, but talking to the daemon directly instead of via dockerode.
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

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

/** A long-running service container (orchestrator or web), as opposed to the
 * one-shot, disposable build containers BuildParams describes. */
struct ServiceContainerSpec {
  std::string name;    // deterministic name, e.g. "ebl-orchestrator" — used for lookup/removal
  std::string image;
  std::vector<std::string> env;
  std::vector<std::string> binds;  // "host-path:container-path[:ro]"
  std::string network;             // network name to attach to (created if missing)
  // {containerPort ("4001/tcp"), hostPort ("4001")} — published on 127.0.0.1 only.
  std::vector<std::pair<std::string, std::string>> portBindings;
};

class DockerClient {
public:
  explicit DockerClient(std::string socketPath);

  /** True if the Docker daemon is reachable at all over the configured socket —
   * never throws, used by `ebl setup` to distinguish "not installed"/"not running"
   * from a real error. */
  bool ping();

  bool imageExists(const std::string& tag);

  /** Tars `contextDir` and POSTs it to /build, invoking onLog for each line of
   * build output. Throws if the daemon reports an error. */
  void buildImage(const std::string& contextDir, const std::string& tag,
                   const std::function<void(const std::string&)>& onLog);

  /** Pulls `tag` from its registry (Docker Hub unless the tag names another
   * registry host), invoking onLog for each status line. Throws on failure — e.g.
   * the tag doesn't exist, or there's no network access. */
  void pullImage(const std::string& tag, const std::function<void(const std::string&)>& onLog);

  void ensureVolume(const std::string& name);
  void ensureNetwork(const std::string& name);

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

  // --- long-running service containers (orchestrator, web) -------------------------

  /** nullopt if no container with this name exists (running or stopped). */
  std::optional<std::string> findContainerIdByName(const std::string& name);
  bool isContainerRunning(const std::string& id);

  /** Creates (but does not start) a detached, auto-restarting service container.
   * If a container with this name already exists, it's removed first (fresh config
   * on every `ebl start`, rather than silently reusing stale settings). */
  std::string createServiceContainer(const ServiceContainerSpec& spec);

  /** Force-removes the named container if it exists; no-op otherwise. */
  void removeContainerByName(const std::string& name);

private:
  HttpClient http_;
};

}  // namespace ebl
