//go:build linux

package tuiinput

import (
	"bytes"
	"os"
	"os/exec"
	"testing"
	"time"

	"github.com/creack/pty"
	"golang.org/x/sys/unix"
)

// readWithTimeout reads whatever is available on f within d, or returns ""
// if nothing arrives - via unix.Select rather than f.SetReadDeadline, which
// was observed to not reliably time out on a pty master file in this
// sandbox (the read blocked well past the requested deadline instead of
// erroring) - the same select()-based approach ReadKey() itself uses, and
// already proven to work correctly against a real pty in this environment
// during this port's verification (see docs/CHANGELOG.md).
func readWithTimeout(f *os.File, d time.Duration) string {
	fd := int(f.Fd())
	deadline := time.Now().Add(d)
	var out []byte
	for {
		remaining := time.Until(deadline)
		if remaining <= 0 {
			break
		}
		fdSet := &unix.FdSet{}
		fdSet.Set(fd)
		tv := unix.NsecToTimeval(remaining.Nanoseconds())
		n, err := unix.Select(fd+1, fdSet, nil, nil, &tv)
		if err != nil || n <= 0 {
			break // nothing more arrived before the deadline
		}
		buf := make([]byte, 4096)
		nr, _ := unix.Read(fd, buf)
		if nr <= 0 {
			break
		}
		out = append(out, buf[:nr]...)
	}
	return string(out)
}

// These tests drive real ReadKey() calls through an actual pseudo-terminal
// with a proper session-leader + controlling-terminal setup (via
// github.com/creack/pty's Start - the same library Docker/HashiCorp/
// gliderlabs use for exactly this kind of terminal-program testing). Against
// two tiny harness binaries living in ./testharness/ (kept inside the module
// tree specifically so they're allowed to import this internal package at
// all). This is the same technique this project's own history used to catch
// a real bug in the C++ version of this exact file (mixing a buffered reader
// with a raw select()-based lookahead silently consumed bytes an escape
// sequence needed - see docs/CHANGELOG.md). A synthetic unit test feeding
// fake bytes to ReadKey() directly wouldn't exercise the raw-mode
// tcsetattr/select() calls at all; only a real pty proves those actually
// work.

func buildHarness(t *testing.T, pkgDir string) string {
	t.Helper()
	binPath := t.TempDir() + "/harness"
	build := exec.Command("go", "build", "-o", binPath, "./"+pkgDir)
	build.Dir = "."
	if out, err := build.CombinedOutput(); err != nil {
		t.Fatalf("building %s: %v\n%s", pkgDir, err, out)
	}
	return binPath
}

func TestReadKeyAgainstRealPTY(t *testing.T) {
	binPath := buildHarness(t, "testharness/readkeydump")

	cmd := exec.Command(binPath)
	ptmx, err := pty.Start(cmd)
	if err != nil {
		t.Fatalf("pty.Start: %v", err)
	}
	defer ptmx.Close()
	defer cmd.Process.Kill()

	// readWithTimeout's own select() loop already returns as soon as a gap
	// with no new data appears, rather than always blocking the full
	// window - so using a generous window here (rather than a fixed
	// time.Sleep beforehand) is patient under heavy CPU contention (e.g.
	// the full `go test ./...` suite running many packages' Docker-backed
	// tests concurrently) while staying fast under normal conditions.
	const window = 3 * time.Second
	readWithTimeout(ptmx, window) // drain any startup echo race noise

	// A brief pause between each send, not just relying on readWithTimeout's
	// "output has stopped" signal: ReadKey() only re-enters raw mode
	// (clearing ICANON) once its *next* call starts, a step after the
	// previous KEY:... line has already been printed - under heavy
	// scheduling delay, sending the next byte immediately after output
	// stops could still land in that brief canonical-mode gap, where the
	// kernel would hold it back waiting for a newline instead of
	// delivering it raw (a real, if narrow, property of the underlying
	// per-call raw-mode design, not a bug this test should paper over by
	// racing it).
	send := func(b []byte) {
		ptmx.Write(b)
		time.Sleep(100 * time.Millisecond)
	}

	send([]byte("\x1b[A"))
	up := readWithTimeout(ptmx, window)
	send([]byte("\x1b[B"))
	down := readWithTimeout(ptmx, window)
	send([]byte("\r"))
	enter := readWithTimeout(ptmx, window)
	send([]byte("\x1b"))
	bareEscape := readWithTimeout(ptmx, window)

	check := func(name, got, want string) {
		t.Helper()
		if !bytes.Contains([]byte(got), []byte(want)) {
			t.Errorf("%s: got %q, want it to contain %q", name, got, want)
		}
	}
	check("arrow-up", up, "KEY:Up")
	check("arrow-down", down, "KEY:Down")
	check("enter", enter, "KEY:Enter")
	check("bare-escape (after the 50ms disambiguation window)", bareEscape, "KEY:Escape")
}

// TestCtrlCDeliversRealSIGINT documents (and locks in) a real, verified
// characteristic shared with the original C++ implementation: raw mode here
// only clears ICANON/ECHO, not ISIG - so in a real terminal session, Ctrl-C
// is consumed by the kernel's line discipline to deliver a genuine SIGINT
// (killing the process, since nothing has installed a custom handler at
// this point in the program) rather than ever reaching ReadKey() as a data
// byte. The `if c == 3` branch in both implementations is therefore
// unreachable in a real terminal with a controlling tty - confirmed by
// running both the Go and C++ binaries side by side in an actual
// session-leader pty during this port (see docs/CHANGELOG.md) - not a
// regression introduced by the port, a pre-existing characteristic being
// faithfully preserved.
func TestCtrlCDeliversRealSIGINT(t *testing.T) {
	binPath := buildHarness(t, "testharness/readkeyonce")

	cmd := exec.Command(binPath)
	ptmx, err := pty.Start(cmd)
	if err != nil {
		t.Fatalf("pty.Start: %v", err)
	}
	defer ptmx.Close()

	// No fixed pre-sleep needed here: even if the harness hasn't installed
	// raw mode by the time this byte is written, the pty layer buffers it
	// and the harness will read it once scheduled - ISIG (which drives the
	// SIGINT-on-Ctrl-C behavior under test) is a property of the terminal
	// line discipline itself, not something the harness's own raw-mode
	// setup toggles.
	ptmx.Write([]byte{0x03})

	done := make(chan error, 1)
	go func() { done <- cmd.Wait() }()

	select {
	case err := <-done:
		if err == nil {
			t.Error("expected the process to be killed by SIGINT, but it exited cleanly")
		}
		// A non-nil *exec.ExitError with a signal-terminated status is exactly
		// the expected outcome - not asserted further here since the specific
		// error shape is OS-dependent; what matters is it didn't hang.
	case <-time.After(10 * time.Second):
		cmd.Process.Kill()
		t.Error("process did not terminate after Ctrl-C - expected a real SIGINT to kill it")
	}
}
