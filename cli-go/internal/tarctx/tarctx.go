// Package tarctx packs a directory into an in-memory tar archive for Docker's
// POST /build context upload. Replaces cli/src/tar_writer.cpp's hand-rolled
// USTAR header/checksum writer entirely - Go's archive/tar already does this
// correctly (and handles more edge cases, like long paths via PAX headers,
// than the C++ version's plain-ustar-only 100-byte name field ever did).
package tarctx

import (
	"archive/tar"
	"bytes"
	"fmt"
	"io"
	"io/fs"
	"os"
	"path/filepath"
)

// FromDirectory walks dirPath and returns a tar archive of every regular file
// in it, with paths relative to dirPath (forward-slashed, matching Docker's
// own build-context convention regardless of host OS).
func FromDirectory(dirPath string) ([]byte, error) {
	info, err := os.Stat(dirPath)
	if err != nil {
		return nil, fmt.Errorf("tarctx: %w", err)
	}
	if !info.IsDir() {
		return nil, fmt.Errorf("tarctx: not a directory: %s", dirPath)
	}

	var buf bytes.Buffer
	tw := tar.NewWriter(&buf)

	err = filepath.WalkDir(dirPath, func(path string, d fs.DirEntry, err error) error {
		if err != nil {
			return err
		}
		if d.IsDir() {
			return nil
		}
		if !d.Type().IsRegular() {
			return nil
		}

		rel, err := filepath.Rel(dirPath, path)
		if err != nil {
			return err
		}
		relSlash := filepath.ToSlash(rel)

		fi, err := d.Info()
		if err != nil {
			return fmt.Errorf("tarctx: %w", err)
		}

		f, err := os.Open(path)
		if err != nil {
			return fmt.Errorf("tarctx: cannot open %s: %w", path, err)
		}
		defer f.Close()

		hdr := &tar.Header{
			Name:    relSlash,
			Mode:    int64(fi.Mode().Perm()),
			Size:    fi.Size(),
			ModTime: fi.ModTime(),
		}
		if err := tw.WriteHeader(hdr); err != nil {
			return fmt.Errorf("tarctx: %w", err)
		}
		if _, err := io.Copy(tw, f); err != nil {
			return fmt.Errorf("tarctx: %w", err)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}

	if err := tw.Close(); err != nil {
		return nil, fmt.Errorf("tarctx: %w", err)
	}
	return buf.Bytes(), nil
}
