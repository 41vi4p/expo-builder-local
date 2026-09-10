package tarctx

import (
	"archive/tar"
	"bytes"
	"io"
	"os"
	"path/filepath"
	"testing"
)

func TestPacksRegularFilesWithRelativeForwardSlashedNames(t *testing.T) {
	dir := t.TempDir()
	if err := os.WriteFile(filepath.Join(dir, "Dockerfile"), []byte("FROM scratch\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.MkdirAll(filepath.Join(dir, "scripts"), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(dir, "scripts", "entrypoint.sh"), []byte("#!/bin/sh\necho hi\n"), 0o755); err != nil {
		t.Fatal(err)
	}

	data, err := FromDirectory(dir)
	if err != nil {
		t.Fatalf("FromDirectory: %v", err)
	}

	tr := tar.NewReader(bytes.NewReader(data))
	found := map[string]string{}
	for {
		hdr, err := tr.Next()
		if err == io.EOF {
			break
		}
		if err != nil {
			t.Fatalf("tar.Reader.Next: %v", err)
		}
		content, err := io.ReadAll(tr)
		if err != nil {
			t.Fatalf("reading %s: %v", hdr.Name, err)
		}
		found[hdr.Name] = string(content)
	}

	if found["Dockerfile"] != "FROM scratch\n" {
		t.Errorf("Dockerfile content = %q", found["Dockerfile"])
	}
	if found["scripts/entrypoint.sh"] != "#!/bin/sh\necho hi\n" {
		t.Errorf("scripts/entrypoint.sh content = %q", found["scripts/entrypoint.sh"])
	}
	if len(found) != 2 {
		t.Errorf("found %d entries, want 2: %v", len(found), found)
	}
}

func TestErrorsOnNonDirectory(t *testing.T) {
	dir := t.TempDir()
	file := filepath.Join(dir, "notadir")
	if err := os.WriteFile(file, []byte("x"), 0o644); err != nil {
		t.Fatal(err)
	}
	if _, err := FromDirectory(file); err == nil {
		t.Error("expected an error for a non-directory path")
	}
}

func TestErrorsOnMissingDirectory(t *testing.T) {
	if _, err := FromDirectory(filepath.Join(t.TempDir(), "does-not-exist")); err == nil {
		t.Error("expected an error for a missing directory")
	}
}
