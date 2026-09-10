#!/bin/sh
# Slow variant of ../fakerunner/entrypoint.sh - for
# TestRunBuildCancellationRemovesTheContainer, which needs enough runway to
# send Ctrl-C mid-build before the fake build would otherwise finish.
set -e
echo "@@PHASE:setup:Preparing project"
echo "@@PROGRESS:100"
echo "@@PHASE:gradle:Running a long Gradle build"
sleep 30
echo "@@PHASE:collect:Collecting artifact"
exit 0
