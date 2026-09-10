#pragma once
// Post-build artifact metrics: size, SHA-256, version/application ID pulled from the
// Gradle project that actually produced the artifact, and git commit/branch. Runs
// entirely on the host filesystem (the CLI executes natively, not in a container), so
// no Android tooling is needed here — just OpenSSL for the hash and optionally git.
#include <cstdint>
#include <string>

namespace ebl {

struct ArtifactMetrics {
  uint64_t sizeBytes = 0;
  std::string sha256;
  std::string versionName;
  std::string versionCode;
  std::string applicationId;
  std::string gitCommit;
  std::string gitBranch;
};

ArtifactMetrics extractArtifactMetrics(const std::string& appPath, const std::string& artifactPath);

}  // namespace ebl
