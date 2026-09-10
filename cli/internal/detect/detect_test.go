package detect

import (
	"os"
	"path/filepath"
	"testing"
)

// Ported 1:1 from cli/tests/test_detect.cpp.

func writeFile(t *testing.T, path, content string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(content), 0o644); err != nil {
		t.Fatalf("writeFile(%s): %v", path, err)
	}
}

func TestDetectsValidExpoProject(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, filepath.Join(dir, "package.json"), `{"name":"my-app","version":"1.0.0","dependencies":{"expo":"^52.0.0"}}`)
	writeFile(t, filepath.Join(dir, "app.json"), `{"expo":{"owner":"project-cell"}}`)
	writeFile(t, filepath.Join(dir, "eas.json"), `{"build":{"preview":{},"production":{}}}`)

	info := ExpoProject(dir)
	if !info.IsExpoProject {
		t.Fatal("expected IsExpoProject = true")
	}
	if info.Name != "my-app" {
		t.Errorf("Name = %q, want %q", info.Name, "my-app")
	}
	if info.Version != "1.0.0" {
		t.Errorf("Version = %q, want %q", info.Version, "1.0.0")
	}
	if info.Owner != "project-cell" {
		t.Errorf("Owner = %q, want %q", info.Owner, "project-cell")
	}
	if len(info.EasProfiles) != 2 {
		t.Errorf("len(EasProfiles) = %d, want 2", len(info.EasProfiles))
	}
	if len(info.EasProfiles) == 2 && (info.EasProfiles[0] != "preview" || info.EasProfiles[1] != "production") {
		t.Errorf("EasProfiles = %v, want [preview production] (order preserved)", info.EasProfiles)
	}
}

func TestRejectsMissingPackageJson(t *testing.T) {
	dir := t.TempDir()
	info := ExpoProject(dir)
	if info.IsExpoProject {
		t.Error("expected IsExpoProject = false")
	}
	if info.Reason == "" {
		t.Error("expected a non-empty Reason")
	}
}

func TestRejectsNonExpoPackageJson(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, filepath.Join(dir, "package.json"), `{"name":"not-expo","dependencies":{"react":"^18.0.0"}}`)
	info := ExpoProject(dir)
	if info.IsExpoProject {
		t.Error("expected IsExpoProject = false")
	}
}

func TestAcceptsExpoAsDevDependency(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, filepath.Join(dir, "package.json"), `{"name":"my-app","devDependencies":{"expo":"^52.0.0"}}`)
	info := ExpoProject(dir)
	if !info.IsExpoProject {
		t.Error("expected IsExpoProject = true")
	}
}

func TestToleratesMalformedAppJsonWithoutFailingDetection(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, filepath.Join(dir, "package.json"), `{"name":"my-app","dependencies":{"expo":"^52.0.0"}}`)
	writeFile(t, filepath.Join(dir, "app.json"), "{not valid json")
	info := ExpoProject(dir)
	if !info.IsExpoProject {
		t.Error("expected IsExpoProject = true")
	}
	if info.Owner != "" {
		t.Errorf("Owner = %q, want empty", info.Owner)
	}
}

func TestRejectsMalformedPackageJson(t *testing.T) {
	dir := t.TempDir()
	writeFile(t, filepath.Join(dir, "package.json"), "{not valid json")
	info := ExpoProject(dir)
	if info.IsExpoProject {
		t.Error("expected IsExpoProject = false")
	}
}
