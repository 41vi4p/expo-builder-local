//go:build windows

package tuiinput

import (
	"os"
	"unsafe"

	"golang.org/x/sys/windows"
)

// UNVERIFIED ON REAL WINDOWS HARDWARE - written from documented Win32
// console API behavior (INPUT_RECORD/KEY_EVENT_RECORD layout and
// ReadConsoleInputW semantics are stable, publicly documented Win32 ABI);
// there's no Windows machine available to actually run this on, the same
// standing caveat this project applies to every other Windows-specific
// branch (see ../../CLAUDE.md). Report anything that doesn't work.
//
// golang.org/x/sys/windows doesn't wrap ReadConsoleInputW, so this declares
// the syscall by hand - a well-documented, ABI-stable kernel32.dll export,
// the same category of "one syscall x/sys doesn't happen to wrap yet" every
// Go project touching low-level Windows APIs occasionally has to do.
//
// This uses ReadConsoleInputW's structured key-event records rather than
// trying to replicate the C++ version's conio.h _getch() byte-stream
// protocol (0/0xE0 extended-key prefix + scan code) - Go has no _getch()
// equivalent in its standard library or in golang.org/x/sys/windows, and
// ReadConsoleInputW is the more direct, documented way to get an
// unambiguous virtual-key code (VK_UP/VK_DOWN/VK_RETURN/VK_ESCAPE) with no
// prefix-byte disambiguation needed at all.
var (
	kernel32              = windows.NewLazySystemDLL("kernel32.dll")
	procReadConsoleInputW = kernel32.NewProc("ReadConsoleInputW")
)

const keyEvent = 0x0001

const (
	vkUp     = 0x26
	vkDown   = 0x28
	vkReturn = 0x0D
	vkEscape = 0x1B
	vkC      = 0x43
)

const (
	leftCtrlPressed  = 0x0008
	rightCtrlPressed = 0x0004
)

// keyEventRecord mirrors Win32's KEY_EVENT_RECORD (16 bytes: BOOL, WORD,
// WORD, WORD, union{WCHAR;CHAR}, DWORD).
type keyEventRecord struct {
	bKeyDown          int32
	wRepeatCount      uint16
	wVirtualKeyCode   uint16
	wVirtualScanCode  uint16
	unicodeChar       uint16
	dwControlKeyState uint32
}

// inputRecord mirrors Win32's INPUT_RECORD (20 bytes: WORD EventType + 2
// bytes padding to a 4-byte boundary, then a 16-byte union whose largest
// member is KEY_EVENT_RECORD/MOUSE_EVENT_RECORD).
type inputRecord struct {
	eventType uint16
	_         uint16 // padding
	event     [16]byte
}

func (r *inputRecord) keyEvent() *keyEventRecord {
	return (*keyEventRecord)(unsafe.Pointer(&r.event[0]))
}

func readConsoleInputW(handle windows.Handle) (inputRecord, bool) {
	var rec inputRecord
	var read uint32
	ret, _, _ := procReadConsoleInputW.Call(
		uintptr(handle),
		uintptr(unsafe.Pointer(&rec)),
		1,
		uintptr(unsafe.Pointer(&read)),
	)
	return rec, ret != 0 && read == 1
}

// ReadKey blocks until one navigation keypress arrives on the console.
func ReadKey() Key {
	handle := windows.Handle(os.Stdin.Fd())

	var oldMode uint32
	if err := windows.GetConsoleMode(handle, &oldMode); err == nil {
		// Clear line input, echo, and processed input (the last one means
		// Ctrl-C arrives as a normal key event instead of terminating the
		// process - matches conio.h's _getch() behavior, which also bypasses
		// the usual Ctrl-C signal).
		newMode := oldMode &^ (windows.ENABLE_LINE_INPUT | windows.ENABLE_ECHO_INPUT | windows.ENABLE_PROCESSED_INPUT)
		windows.SetConsoleMode(handle, newMode)
		defer windows.SetConsoleMode(handle, oldMode)
	}

	for {
		rec, ok := readConsoleInputW(handle)
		if !ok {
			return Other
		}
		if rec.eventType != keyEvent {
			continue
		}
		key := rec.keyEvent()
		if key.bKeyDown == 0 {
			continue // ignore key-up events, only act on key-down
		}
		switch key.wVirtualKeyCode {
		case vkUp:
			return Up
		case vkDown:
			return Down
		case vkReturn:
			return Enter
		case vkEscape:
			return Escape
		case vkC:
			if key.dwControlKeyState&(leftCtrlPressed|rightCtrlPressed) != 0 {
				return Escape // Ctrl-C, with ENABLE_PROCESSED_INPUT off above
			}
			return Other
		default:
			return Other
		}
	}
}
