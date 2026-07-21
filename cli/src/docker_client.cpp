#include "docker_client.hpp"

#include <stdexcept>

#include "json.hpp"
#include "tar_writer.hpp"

namespace ebl {

DockerClient::DockerClient(std::string socketPath) : http_(std::move(socketPath)) {}

bool DockerClient::imageExists(const std::string& tag) {
  Json filters = Json::object();
  Json refs = Json::array();
  refs.push_back(Json(tag));
  filters.set("reference", refs);

  std::string path = "/images/json?filters=" + urlEncode(filters.dump());
  HttpResponse res = http_.request("GET", path);
  if (res.status != 200) {
    throw std::runtime_error("Failed to query Docker images (HTTP " + std::to_string(res.status) + "): " + res.body);
  }
  Json images = Json::parse(res.body);
  return images.size() > 0;
}

void DockerClient::buildImage(const std::string& contextDir, const std::string& tag,
                               const std::function<void(const std::string&)>& onLog) {
  std::string tar = createTarFromDirectory(contextDir);
  std::string path = "/build?t=" + urlEncode(tag) + "&rm=1";

  std::string residual;
  std::string firstError;

  auto onChunk = [&](const char* data, size_t len) {
    residual.append(data, len);
    size_t pos;
    while ((pos = residual.find('\n')) != std::string::npos) {
      std::string line = residual.substr(0, pos);
      residual.erase(0, pos + 1);
      if (line.empty()) continue;

      Json event;
      try {
        event = Json::parse(line);
      } catch (const JsonError&) {
        onLog(line);  // not a JSON line (shouldn't normally happen) — show it verbatim
        continue;
      }
      if (event.contains("stream")) {
        onLog(event.at("stream").asString());
      } else if (event.contains("status")) {
        std::string status = event.at("status").asString();
        if (event.contains("progress")) status += " " + event.at("progress").asString();
        onLog(status + "\n");
      } else if (event.contains("error")) {
        firstError = event.at("error").asString();
      }
    }
  };

  long status = http_.streamRequest("POST", path, tar, {"Content-Type: application/x-tar"}, onChunk);

  if (!firstError.empty()) {
    throw std::runtime_error("Docker build failed: " + firstError);
  }
  if (status != 200) {
    throw std::runtime_error("Docker build request failed (HTTP " + std::to_string(status) + ")");
  }
}

void DockerClient::ensureVolume(const std::string& name) {
  Json body = Json::object();
  body.set("Name", name);
  // Idempotent: if a volume with this name already exists, Docker returns it as-is
  // rather than erroring, since we never pass driver-specific options that could conflict.
  HttpResponse res = http_.request("POST", "/volumes/create", body.dump(), {"Content-Type: application/json"});
  if (res.status != 201 && res.status != 200) {
    throw std::runtime_error("Failed to create Docker volume \"" + name + "\" (HTTP " +
                              std::to_string(res.status) + "): " + res.body);
  }
}

std::string DockerClient::createContainer(const BuildParams& params, const std::string& runnerImage,
                                           const std::string& gradleCacheVolume, const std::string& npmCacheVolume,
                                           unsigned int buildUid, unsigned int buildGid) {
  Json env = Json::array();
  env.push_back(Json("APP_DIR=/work/app"));
  env.push_back(Json("ARTIFACT_TYPE=" + params.artifactType));
  env.push_back(Json("PROFILE=" + params.profile));
  env.push_back(Json("ENGINE=" + params.engine));
  env.push_back(Json("SIGNING_MODE=" + params.signingMode));
  env.push_back(Json("BUILD_UID=" + std::to_string(buildUid)));
  env.push_back(Json("BUILD_GID=" + std::to_string(buildGid)));
  if (!params.expoToken.empty()) env.push_back(Json("EXPO_TOKEN=" + params.expoToken));

  Json binds = Json::array();
  binds.push_back(Json(params.appPath + ":/work/app"));
  binds.push_back(Json(gradleCacheVolume + ":/cache/gradle"));
  binds.push_back(Json(npmCacheVolume + ":/cache/npm"));

  if (params.signingMode == "release" && params.hasKeystore) {
    std::string containerPath = "/keystores/" + params.keystore.filename;
    binds.push_back(Json(params.keystore.hostPath + ":" + containerPath + ":ro"));
    env.push_back(Json("KEYSTORE_PATH=" + containerPath));
    env.push_back(Json("KEYSTORE_PASSWORD=" + params.keystore.storePassword));
    env.push_back(Json("KEY_ALIAS=" + params.keystore.keyAlias));
    env.push_back(Json("KEY_PASSWORD=" +
                        (params.keystore.keyPassword.empty() ? params.keystore.storePassword
                                                              : params.keystore.keyPassword)));
  }

  Json hostConfig = Json::object();
  hostConfig.set("Binds", binds);
  hostConfig.set("AutoRemove", Json(false));

  Json body = Json::object();
  body.set("Image", Json(runnerImage));
  body.set("Env", env);
  body.set("Tty", Json(true));
  body.set("OpenStdin", Json(false));
  body.set("AttachStdout", Json(true));
  body.set("AttachStderr", Json(true));
  body.set("WorkingDir", Json("/work/app"));
  body.set("HostConfig", hostConfig);

  HttpResponse res = http_.request("POST", "/containers/create", body.dump(), {"Content-Type: application/json"});
  if (res.status != 201) {
    Json errJson;
    std::string message = res.body;
    try {
      errJson = Json::parse(res.body);
      if (errJson.contains("message")) message = errJson.at("message").asString();
    } catch (const JsonError&) {
    }
    throw std::runtime_error("Failed to create build container (HTTP " + std::to_string(res.status) +
                              "): " + message);
  }

  Json parsed = Json::parse(res.body);
  return parsed.at("Id").asString();
}

void DockerClient::startContainer(const std::string& id) {
  HttpResponse res = http_.request("POST", "/containers/" + id + "/start");
  if (res.status != 204 && res.status != 304) {
    throw std::runtime_error("Failed to start build container (HTTP " + std::to_string(res.status) + "): " + res.body);
  }
}

void DockerClient::attachAndStream(const std::string& id, const std::function<void(const char*, size_t)>& onChunk) {
  std::string path = "/containers/" + id + "/attach?stream=1&stdout=1&stderr=1";
  http_.streamRequest("POST", path, "", {}, onChunk);
}

int DockerClient::waitContainer(const std::string& id) {
  // No timeout: a real build can run for many minutes.
  HttpResponse res = http_.request("POST", "/containers/" + id + "/wait", "", {}, /*timeoutSeconds=*/0);
  if (res.status != 200) {
    throw std::runtime_error("Failed waiting for the build container (HTTP " + std::to_string(res.status) +
                              "): " + res.body);
  }
  Json parsed = Json::parse(res.body);
  return static_cast<int>(parsed.at("StatusCode").asInt());
}

void DockerClient::removeContainer(const std::string& id) {
  HttpResponse res = http_.request("DELETE", "/containers/" + id + "?force=1");
  if (res.status != 204 && res.status != 404) {
    throw std::runtime_error("Failed to remove build container (HTTP " + std::to_string(res.status) + "): " + res.body);
  }
}

}  // namespace ebl
