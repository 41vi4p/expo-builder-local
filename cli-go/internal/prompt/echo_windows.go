//go:build windows

package prompt

import "golang.org/x/sys/windows"

// disableEcho clears ENABLE_ECHO_INPUT and returns a restore function.
// Direct port of cli/src/prompt.cpp's Windows branch
// (GetConsoleMode/SetConsoleMode).
func disableEcho(fd int) (restore func(), err error) {
	handle := windows.Handle(fd)
	var oldMode uint32
	if err := windows.GetConsoleMode(handle, &oldMode); err != nil {
		return nil, err
	}
	if err := windows.SetConsoleMode(handle, oldMode&^windows.ENABLE_ECHO_INPUT); err != nil {
		return nil, err
	}
	return func() {
		_ = windows.SetConsoleMode(handle, oldMode)
	}, nil
}
