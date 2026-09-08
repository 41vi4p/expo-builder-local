#pragma once
// Windows-only: provisions (or detects/reuses) the JDK/Android SDK/Node toolchain
// the native build engine (native_build.hpp) needs instead of a Docker container —
// see ../CLAUDE.md's native-engine section. UNVERIFIED ON REAL WINDOWS HARDWARE.
#include <functional>
#include <string>

#include "config_store.hpp"

namespace ebl {

/** Detects an existing JDK 17 / Android SDK / Node install first (never touches or
 * reconfigures it — just records its path with installedByEbl=false), and
 * downloads+installs an ebl-owned copy of anything missing under
 * %LOCALAPPDATA%\ebl\toolchain\{jdk17,android-sdk,node}, isolated from any
 * system-wide install so a later uninstall can safely remove exactly what it
 * downloaded and nothing else (see NativeToolchainConfig's per-component
 * installedByEbl flags). The Android SDK components installed match
 * docker/runner/Dockerfile's own pinned versions exactly (platforms 35/34,
 * build-tools 35.0.0, ndk 27.1.12297006, cmake 3.22.1) so native and Docker builds
 * stay equivalent.
 *
 * Idempotent: re-running with a `toolchain` that already has valid, still-existing
 * paths recorded skips re-downloading that component.
 *
 * `onLog` receives human-readable progress lines (this is `ebl setup`'s output, not
 * the @@marker build protocol). Mutates `toolchain` in place with the resolved
 * paths/flags — the caller (commands/setup.cpp) is responsible for
 * saveConfig(cfg) afterward. Throws if a component can neither be detected nor
 * installed. */
void provisionNativeToolchain(NativeToolchainConfig& toolchain, const std::function<void(const std::string&)>& onLog);

}  // namespace ebl
