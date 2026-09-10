//go:build linux || darwin

package tuiinput

import (
	"os"
	"time"

	"golang.org/x/sys/unix"
)

// enterRawMode puts stdin into raw mode (no echo, no line buffering -
// read() returns as soon as a single byte is available) and returns a
// restore function - the direct analog of cli/src/tui_input.cpp's
// RawModeGuard, called fresh on every ReadKey() so a failure between here
// and the deferred restore can never leave the terminal stuck in raw mode
// (same reasoning the C++ RAII guard documents).
func enterRawMode(fd int) (restore func(), ok bool) {
	old, err := unix.IoctlGetTermios(fd, getTermiosReq)
	if err != nil {
		return func() {}, false
	}
	raw := *old
	raw.Lflag &^= unix.ICANON | unix.ECHO
	raw.Cc[unix.VMIN] = 1
	raw.Cc[unix.VTIME] = 0
	if err := unix.IoctlSetTermios(fd, setTermiosReq, &raw); err != nil {
		return func() {}, false
	}
	return func() { _ = unix.IoctlSetTermios(fd, setTermiosReq, old) }, true
}

// moreInputWithin reports whether another byte arrives on fd within d -
// equivalent to the C++ version's select()-based moreInputWithin().
func moreInputWithin(fd int, d time.Duration) bool {
	fdSet := &unix.FdSet{}
	fdSet.Set(fd)
	tv := unix.NsecToTimeval(d.Nanoseconds())
	n, err := unix.Select(fd+1, fdSet, nil, nil, &tv)
	return err == nil && n > 0
}

// readByte is a raw read() directly on the fd - deliberately not going
// through any buffered reader. Mixing a buffered reader with a raw
// select()-based lookahead on the same fd is unsafe: a single buffered read
// can slurp an entire multi-byte escape sequence into its own internal
// buffer at once, leaving nothing at the kernel level for a later select()
// to see - this exact bug was found and fixed in the C++ version of this
// file via a real pty-driven test (see docs/CHANGELOG.md), and this Go port
// avoids it the same way: one byte in, one byte out, always straight from
// the fd via unix.Read, never through bufio/os.Stdin's own buffering.
func readByte(fd int) (byte, bool) {
	buf := make([]byte, 1)
	n, err := unix.Read(fd, buf)
	if n != 1 || err != nil {
		return 0, false
	}
	return buf[0], true
}

// ReadKey blocks until one navigation keypress arrives on stdin.
func ReadKey() Key {
	fd := int(os.Stdin.Fd())
	restore, _ := enterRawMode(fd)
	defer restore()

	c, ok := readByte(fd)
	if !ok {
		return Other
	}
	if c == 27 {
		if !moreInputWithin(fd, 50*time.Millisecond) {
			return Escape // nothing followed - a bare Escape press
		}
		c2, ok := readByte(fd)
		if !ok {
			return Escape
		}
		if c2 == '[' {
			if !moreInputWithin(fd, 50*time.Millisecond) {
				return Other
			}
			c3, ok := readByte(fd)
			if !ok {
				return Other
			}
			switch c3 {
			case 'A':
				return Up
			case 'B':
				return Down
			default:
				return Other
			}
		}
		return Escape
	}
	if c == '\r' || c == '\n' {
		return Enter
	}
	if c == 3 {
		return Escape // Ctrl-C - raw mode means no real SIGINT for it
	}
	return Other
}
