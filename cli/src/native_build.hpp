#pragma once
// Windows-only native build engine - the direct analog of
// docker/runner/build-entrypoint.sh, running as real host processes against the
// provisioned/detected toolchain (native_toolchain.hpp) instead of inside a
// container. See ../CLAUDE.md's native-engine section. UNVERIFIED ON REAL WINDOWS
// HARDWARE.
#include <functional>

#include "config_store.hpp"
#include "docker_client.hpp"  // BuildParams

namespace ebl {

/** Runs one build end to end (install deps, resolve engine, prebuild/eas or
 * gradle, sign, collect the artifact) using `toolchain`'s provisioned JDK/Android
 * SDK/Node instead of a Docker container. `onChunk` receives combined
 * stdout-equivalent output exactly the way DockerClient::attachAndStream does -
 * including the byte-for-byte same `@@PHASE:`/`@@PROGRESS:`/`@@ENGINE:`/
 * `@@BUILD_NUMBER:`/`@@ARTIFACT:`/`@@DURATION:`/`@@ERROR:` marker lines
 * build-entrypoint.sh emits - so commands/build.cpp's existing marker-line parser
 * and success/failure reporting work completely unchanged regardless of which
 * engine actually ran. `@@ARTIFACT:` is emitted as a real host path directly (no
 * container-path prefix to translate back), which toHostArtifactPath's existing
 * prefix-check already passes through unmodified.
 *
 * Returns the same kind of exit code DockerClient::waitContainer would (0 =
 * success), so build.cpp's `exitStatus == 0` check applies unchanged too. */
int runNativeBuild(const BuildParams& params, const NativeToolchainConfig& toolchain,
                    const std::function<void(const char*, size_t)>& onChunk);

}  // namespace ebl
