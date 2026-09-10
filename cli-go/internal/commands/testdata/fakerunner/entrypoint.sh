#!/bin/sh
# Fake runner entrypoint for build_integration_test.go - emits exactly the
# same marker protocol docker/runner/build-entrypoint.sh does (see that
# file's own header comment), but instantly instead of running a real
# Android build. Exercises the real Docker container lifecycle
# (create/attach/wait/stats-poll) and the real marker-parsing/progress-
# tracking/dashboard code in build_run.go end to end, without needing the
# actual multi-GB runner image or a real Gradle build.
set -e

echo "@@PHASE:setup:Preparing project"
sleep 0.1
echo "@@PROGRESS:100"

echo "@@PHASE:install:Installing dependencies"
sleep 0.1
echo "@@PROGRESS:100"

echo "@@PHASE:prebuild:Generating native project"
sleep 0.1
echo "@@PROGRESS:100"

echo "@@PHASE:signing:Configuring signing"
sleep 0.1
echo "@@PROGRESS:100"

echo "@@ENGINE:gradle"
echo "@@PHASE:gradle:Running Gradle build"
echo "<=====-----> 50% EXECUTING"
sleep 0.1
echo "<==========> 100% EXECUTING"
sleep 0.1

echo "@@PHASE:collect:Collecting artifact"
mkdir -p /work/app/ebl_builds/v1.0.0-build1
echo "fake apk contents for integration test" > /work/app/ebl_builds/v1.0.0-build1/app.apk
# Real docker-entrypoint.sh re-homes ownership to BUILD_UID/BUILD_GID so the
# host user can read/clean up build output - this fake entrypoint doesn't
# replicate that whole dance, just makes the result world-writable so the
# bind-mounted host directory (and Go's t.TempDir() cleanup in the test that
# uses this image) isn't left with a root-owned file it can't remove.
chmod -R 777 /work/app/ebl_builds
echo "@@BUILD_NUMBER:1"
echo "@@ARTIFACT:/work/app/ebl_builds/v1.0.0-build1/app.apk"
echo "@@PROGRESS:100"

echo "@@PHASE:done:Build complete"
exit 0
