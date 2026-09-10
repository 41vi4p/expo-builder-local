package runnerctx

import (
	"os"
	"path/filepath"
	"testing"
)

func TestMaterializeToTempDirProducesExpectedFiles(t *testing.T) {
	dir, cleanup, err := MaterializeToTempDir()
	if err != nil {
		t.Fatalf("MaterializeToTempDir: %v", err)
	}
	defer cleanup()

	for _, rel := range []string{
		"Dockerfile",
		"build-entrypoint.sh",
		"docker-entrypoint.sh",
		filepath.Join("scripts", "patch-android-signing.js"),
		filepath.Join("scripts", "write-eas-credentials.js"),
	} {
		path := filepath.Join(dir, rel)
		info, err := os.Stat(path)
		if err != nil {
			t.Errorf("expected %s to exist: %v", rel, err)
			continue
		}
		if info.Size() == 0 {
			t.Errorf("%s materialized as an empty file", rel)
		}
	}
}

func TestMaterializeToTempDirContentMatchesEmbeddedSource(t *testing.T) {
	dir, cleanup, err := MaterializeToTempDir()
	if err != nil {
		t.Fatalf("MaterializeToTempDir: %v", err)
	}
	defer cleanup()

	want, err := embedded.ReadFile(embeddedRoot + "/Dockerfile")
	if err != nil {
		t.Fatalf("reading embedded Dockerfile: %v", err)
	}
	got, err := os.ReadFile(filepath.Join(dir, "Dockerfile"))
	if err != nil {
		t.Fatalf("reading materialized Dockerfile: %v", err)
	}
	if string(got) != string(want) {
		t.Error("materialized Dockerfile content doesn't match the embedded source")
	}
}

func TestCleanupActuallyRemovesTheDirectory(t *testing.T) {
	dir, cleanup, err := MaterializeToTempDir()
	if err != nil {
		t.Fatalf("MaterializeToTempDir: %v", err)
	}
	cleanup()
	if _, err := os.Stat(dir); !os.IsNotExist(err) {
		t.Errorf("expected %s to be removed after cleanup, stat error: %v", dir, err)
	}
}

func TestEachCallProducesAnIndependentDirectory(t *testing.T) {
	dir1, cleanup1, err := MaterializeToTempDir()
	if err != nil {
		t.Fatalf("MaterializeToTempDir: %v", err)
	}
	defer cleanup1()

	dir2, cleanup2, err := MaterializeToTempDir()
	if err != nil {
		t.Fatalf("MaterializeToTempDir: %v", err)
	}
	defer cleanup2()

	if dir1 == dir2 {
		t.Error("expected two independent temp directories")
	}
}
