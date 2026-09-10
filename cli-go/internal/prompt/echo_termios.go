//go:build linux || darwin

package prompt

import "golang.org/x/sys/unix"

// disableEcho clears ECHO (keeping ICANON, i.e. still line-buffered by the
// tty driver - Enter still ends the line) and returns a restore function.
// Matches cli/src/prompt.cpp's POSIX branch (tcgetattr/tcsetattr clearing
// ECHO only, not full raw mode) - the ioctl request numbers differ between
// Linux (TCGETS/TCSETS) and Darwin/BSD (TIOCGETA/TIOCSETA), both handled
// below via build tags on getTermiosReq/setTermiosReq.
func disableEcho(fd int) (restore func(), err error) {
	oldState, err := unix.IoctlGetTermios(fd, getTermiosReq)
	if err != nil {
		return nil, err
	}
	newState := *oldState
	newState.Lflag &^= unix.ECHO
	if err := unix.IoctlSetTermios(fd, setTermiosReq, &newState); err != nil {
		return nil, err
	}
	return func() {
		_ = unix.IoctlSetTermios(fd, setTermiosReq, oldState)
	}, nil
}
