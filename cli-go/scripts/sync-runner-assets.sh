#!/usr/bin/env bash
# Copies ../docker/runner/ (repo root, one level up from cli-go/) into
# internal/runnerctx/assets/runner/ so //go:embed can bundle it into the ebl
# binary - Go's embed directive can't reference a path outside its own
# package directory (no ".." allowed), the same reason cli/CMakeLists.txt has
# its own `file(COPY ../docker/runner ...)` step for the C++ build. Run this
# before every `go build`/`go test`/release (wire it into the Makefile/CI as
# its own step once this becomes cli/'s real build - see ../../CLAUDE.md's
# version-management notes) whenever docker/runner/ changes;
# internal/runnerctx/assets/ itself is gitignored, not committed, so it
# never goes stale in version control by accident.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

SRC="../docker/runner"
DEST="internal/runnerctx/assets/runner"

if [ ! -d "$SRC" ]; then
  echo "sync-runner-assets: $SRC not found (expected at the repo root, one level up from cli-go/)" >&2
  exit 1
fi

rm -rf "$DEST"
mkdir -p "$DEST"
cp -r "$SRC/." "$DEST/"
echo "Synced $SRC -> $DEST"
