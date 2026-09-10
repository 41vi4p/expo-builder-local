//go:build !windows

package commands

import "os"

func buildUIDGID() (uint32, uint32) {
	return uint32(os.Getuid()), uint32(os.Getgid())
}
