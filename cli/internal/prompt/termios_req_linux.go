//go:build linux

package prompt

import "golang.org/x/sys/unix"

const (
	getTermiosReq = unix.TCGETS
	setTermiosReq = unix.TCSETS
)
