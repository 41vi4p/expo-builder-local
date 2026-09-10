// Package runnerctx bundles the Android runner build context
// (docker/runner/ at the repo root: Dockerfile + entrypoint scripts + signing
// helpers) directly into the ebl binary via //go:embed, so a compiled/
// installed binary works without the rest of the repo present - the same
// goal cli/src/runner_context.cpp served via a runtime self-exe-relative
// directory lookup, achieved more simply here since Go can embed the
// content at compile time instead of needing to locate a sibling
// share/expo-builder-local/runner/ directory at runtime.
//
// assets/runner/ (this package's sibling directory) is a build-time-only
// copy, not committed - see ../../scripts/sync-runner-assets.sh, which must
// run (copying ../../../docker/runner/ into it) before this package can be
// built at all, the direct analog of cli/CMakeLists.txt's own
// `file(COPY ../docker/runner ...)` configure-time step for the C++ build.
package runnerctx

import (
	"embed"
	"io/fs"
	"os"
	"path/filepath"
)

//go:embed all:assets/runner
var embedded embed.FS

const embeddedRoot = "assets/runner"

// MaterializeToTempDir extracts the embedded runner context into a fresh
// temporary directory, returning its path and a cleanup function the caller
// must invoke once done with it (e.g. after the build-context tar has been
// created from it).
func MaterializeToTempDir() (dir string, cleanup func(), err error) {
	dir, err = os.MkdirTemp("", "ebl-runner-context-*")
	if err != nil {
		return "", nil, err
	}
	cleanup = func() { os.RemoveAll(dir) }

	sub, err := fs.Sub(embedded, embeddedRoot)
	if err != nil {
		cleanup()
		return "", nil, err
	}

	err = fs.WalkDir(sub, ".", func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		target := filepath.Join(dir, filepath.FromSlash(path))
		if d.IsDir() {
			return os.MkdirAll(target, 0o755)
		}
		data, err := fs.ReadFile(sub, path)
		if err != nil {
			return err
		}
		// The embedded FS doesn't preserve the original executable bit (Go's
		// embed strips file mode metadata) - entrypoint/build scripts under
		// docker/runner/ need +x once materialized, since Docker's build
		// context ADD/COPY-then-RUN steps in the Dockerfile itself restore
		// correctness inside the image, but a local `docker build` reads
		// file modes from this context directory first.
		return os.WriteFile(target, data, 0o755)
	})
	if err != nil {
		cleanup()
		return "", nil, err
	}
	return dir, cleanup, nil
}
