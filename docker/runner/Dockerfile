# syntax=docker/dockerfile:1.7
#
# Runner image: everything needed to turn a managed Expo (SDK 56 / RN 0.85) project
# into a signed Android APK/AAB, either via `eas build --local` or `expo prebuild` + Gradle.
#
# This image is built once (`make build-image` / `docker compose build runner`) and is
# NOT run as a long-lived service — the orchestrator spawns a fresh, disposable container
# from it for every build (see orchestrator/src/docker/runner.ts).

FROM ubuntu:24.04 AS base

ARG ANDROID_CMDLINE_TOOLS_VERSION=11076708
ARG ANDROID_PLATFORM=android-35
ARG ANDROID_PLATFORM_MIN=android-34
ARG ANDROID_BUILD_TOOLS=35.0.0
ARG NDK_VERSION=27.1.12297006

ENV DEBIAN_FRONTEND=noninteractive \
    ANDROID_HOME=/opt/android-sdk \
    ANDROID_SDK_ROOT=/opt/android-sdk \
    JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64 \
    GRADLE_USER_HOME=/cache/gradle \
    NPM_CONFIG_CACHE=/cache/npm

# --- OS deps + JDK 17 -----------------------------------------------------------
# `apt-get upgrade` patches any CVEs already fixed upstream for packages baked into
# the ubuntu:24.04 base layer itself, not just the ones we explicitly install below.
RUN apt-get update && apt-get upgrade -y && apt-get install -y --no-install-recommends \
      curl ca-certificates unzip zip git openssh-client build-essential \
      python3 file gosu openjdk-17-jdk-headless \
    && rm -rf /var/lib/apt/lists/*

# --- Node.js (current LTS, tracked via NodeSource's rolling "lts" channel) -------
RUN curl -fsSL https://deb.nodesource.com/setup_lts.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/* \
    && npm install -g npm@latest eas-cli@latest

# --- Android SDK command-line tools --------------------------------------------
RUN mkdir -p ${ANDROID_HOME}/cmdline-tools \
    && curl -fsSL -o /tmp/cmdline-tools.zip \
       "https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_CMDLINE_TOOLS_VERSION}_latest.zip" \
    && unzip -q /tmp/cmdline-tools.zip -d ${ANDROID_HOME}/cmdline-tools \
    && mv ${ANDROID_HOME}/cmdline-tools/cmdline-tools ${ANDROID_HOME}/cmdline-tools/latest \
    && rm /tmp/cmdline-tools.zip

ENV PATH=${ANDROID_HOME}/cmdline-tools/latest/bin:${ANDROID_HOME}/platform-tools:${PATH}

RUN yes | sdkmanager --licenses >/dev/null \
    && sdkmanager --install \
      "platform-tools" \
      "platforms;${ANDROID_PLATFORM}" \
      "platforms;${ANDROID_PLATFORM_MIN}" \
      "build-tools;${ANDROID_BUILD_TOOLS}" \
      "ndk;${NDK_VERSION}" \
      "cmake;3.22.1" \
    >/dev/null

# --- Non-root build user --------------------------------------------------------
# The orchestrator passes BUILD_UID/BUILD_GID at container-run time (not build time) so
# artifacts written into the bind-mounted host app folder are owned by the host developer,
# not root. We create a default "builder" user here; docker-entrypoint.sh re-homes it via
# gosu if the runtime UID/GID differ.
# Recent ubuntu:24.04 point releases ship a default non-root "ubuntu" user/group
# already occupying UID/GID 1000 — remove it first (no-op on older base images that
# don't have it) so our own UID/GID-1000 "builder" user can be created either way.
RUN (id -u ubuntu >/dev/null 2>&1 && userdel -r ubuntu || true) \
    && (getent group ubuntu >/dev/null 2>&1 && groupdel ubuntu || true) \
    && groupadd -g 1000 builder && useradd -m -u 1000 -g 1000 -s /bin/bash builder \
    && mkdir -p /cache/gradle /cache/npm /work /keystores \
    && chown -R builder:builder /cache /work /opt/android-sdk

COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
COPY build-entrypoint.sh /usr/local/bin/build-entrypoint.sh
COPY scripts/ /usr/local/lib/expo-builder/
RUN chmod +x /usr/local/bin/docker-entrypoint.sh /usr/local/bin/build-entrypoint.sh

WORKDIR /work

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["/usr/local/bin/build-entrypoint.sh"]
