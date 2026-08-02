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

/** Where a build container sees the bind-mounted project root (matches
 * docker/runner/build-entrypoint.sh's default APP_DIR, and
 * orchestrator/src/docker/runner.ts's CONTAINER_APP_DIR) — paths the container
 * reports back (e.g. the `@@ARTIFACT:` marker) are rooted here, not at the real
 * host path, so callers must translate before touching the host filesystem. */
constexpr const char* kContainerAppDir = "/work/app";

/** Docker label key every build container is tagged with (value: the host appPath
 * being built) — lets findRunningBuildContainerByAppPath() detect a build already
 * in flight for a given project without needing a deterministic container name. */
constexpr const char* kAppPathLabel = "com.expo-builder-local.app-path";

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
   * registry host), invoking onEvent for each status line: (layer id, status,
   * progress) — id/progress are empty when the daemon's event doesn't carry them
   * (e.g. an overall "Pulling from ..."/"Status: ..." line). Throws on failure —
   * e.g. the tag doesn't exist, or there's no network access. */
  void pullImage(const std::string& tag,
                  const std::function<void(const std::string& id, const std::string& status,
                                            const std::string& progress)>& onEvent);

  void ensureVolume(const std::string& name);
  void ensureNetwork(const std::string& name);

  std::string createContainer(const BuildParams& params, const std::string& runnerImage,
                               const std::string& gradleCacheVolume, const std::string& npmCacheVolume,
                               unsigned int buildUid, unsigned int buildGid);
  void startContainer(const std::string& id);

  /** Returns the id of an already-running build container for `appPath`, if any —
   * every build container created by createContainer() is labeled with its
   * appPath (see kAppPathLabel), so this is how `ebl build` detects "a build for
   * this project is already in flight" before launching a second one that would
   * just contend with the first over the shared npm/gradle cache volumes. */
  std::optional<std::string> findRunningBuildContainerByAppPath(const std::string& appPath);

  struct BuildContainerInfo {
    std::string id;
    std::string state;  // "running", "exited", ...
  };

  /** Every container (running or stopped) ebl itself created — labeled with
   * kAppPathLabel regardless of which project they were built for. Used by
   * `ebl clean` to find its own leftover containers without guessing names. */
  std::vector<BuildContainerInfo> listBuildContainers();

  /** Deletes a volume by name; no-op (not an error) if it doesn't exist. Fails
   * with Docker's own error if the volume is still in use by a container. */
  void removeVolume(const std::string& name);

  /** Deletes an image by tag; no-op (not an error) if it doesn't exist. */
  void removeImage(const std::string& tag);

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
