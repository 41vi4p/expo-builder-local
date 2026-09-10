//go:build linux

package tuimenu

import (
	"fmt"
	"os"
	"os/exec"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/creack/pty"
	"golang.org/x/sys/unix"
)

// Same real-pty verification technique as internal/tuiinput's own tests -
// see that package's tuiinput_pty_test.go for why a synthetic test feeding
// fake bytes directly wouldn't prove anything about the actual raw-mode
// terminal interaction.

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
			break
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

// Built once and shared across subtests (rebuilding a Go binary per subtest
// added enough scheduling jitter under load to make the tight pty-timing
// windows below occasionally flaky).
var buildHarnessOnce = sync.OnceValues(func() (string, error) {
	dir, err := os.MkdirTemp("", "tuimenu-harness-*")
	if err != nil {
		return "", err
	}
	binPath := dir + "/harness"
	build := exec.Command("go", "build", "-o", binPath, "./testharness/selectmenu")
	if out, err := build.CombinedOutput(); err != nil {
		return "", fmt.Errorf("building harness: %v\n%s", err, out)
	}
	return binPath, nil
})

func buildHarness(t *testing.T) string {
	t.Helper()
	bin, err := buildHarnessOnce()
	if err != nil {
		t.Fatal(err)
	}
	return bin
}

func startHarness(t *testing.T, binPath string, defaultIndex string) *os.File {
	t.Helper()
	cmd := exec.Command(binPath, defaultIndex)
	ptmx, err := pty.Start(cmd)
	if err != nil {
		t.Fatalf("pty.Start: %v", err)
	}
	t.Cleanup(func() {
		ptmx.Close()
		cmd.Process.Kill()
	})
	// readWithTimeout's own select() loop already returns as soon as a gap
	// with no new data appears, rather than always blocking the full
	// window - so a generous window here (rather than a fixed time.Sleep
	// beforehand) is patient under heavy CPU contention (e.g. the full
	// `go test ./...` suite running many packages' Docker-backed tests
	// concurrently) while staying fast under normal conditions.
	readWithTimeout(ptmx, testWindow) // drain the initial render
	return ptmx
}

const testWindow = 3 * time.Second

func TestEnterImmediatelyConfirmsTheDefault(t *testing.T) {
	bin := buildHarness(t)
	ptmx := startHarness(t, bin, "1")

	ptmx.Write([]byte("\r"))
	out := readWithTimeout(ptmx, testWindow)
	if !strings.Contains(out, "RESULT:1") {
		t.Errorf("got %q, want it to contain RESULT:1", out)
	}
}

func TestDownArrowMovesSelectionForward(t *testing.T) {
	bin := buildHarness(t)
	ptmx := startHarness(t, bin, "0")

	ptmx.Write([]byte("\x1b[B")) // Down
	readWithTimeout(ptmx, testWindow)
	// A brief pause here, not just relying on readWithTimeout's "output has
	// stopped" signal: ReadKey() only re-enters raw mode (clearing ICANON)
	// once the *next* call starts, a step after render() has already
	// finished printing - under heavy scheduling delay, sending the next
	// key immediately after output stops could still land in that brief
	// canonical-mode gap, where the kernel would hold it back waiting for a
	// newline instead of delivering it raw. This mirrors a real (if
	// narrow) property of the underlying design, not a bug this test
	// should paper over by racing it.
	time.Sleep(100 * time.Millisecond)
	ptmx.Write([]byte("\r")) // confirm
	out := readWithTimeout(ptmx, testWindow)
	if !strings.Contains(out, "RESULT:1") {
		t.Errorf("got %q, want it to contain RESULT:1 (started at 0, moved down once)", out)
	}
}

func TestUpArrowWrapsAroundFromFirstToLast(t *testing.T) {
	bin := buildHarness(t)
	ptmx := startHarness(t, bin, "0")

	ptmx.Write([]byte("\x1b[A")) // Up, from index 0 - should wrap to the last option (index 2)
	readWithTimeout(ptmx, testWindow)
	time.Sleep(100 * time.Millisecond) // see TestDownArrowMovesSelectionForward's comment on this pause
	ptmx.Write([]byte("\r"))
	out := readWithTimeout(ptmx, testWindow)
	if !strings.Contains(out, "RESULT:2") {
		t.Errorf("got %q, want it to contain RESULT:2 (wrapped from 0 to the last index)", out)
	}
}

func TestEscapeCancelsWithMinusOne(t *testing.T) {
	bin := buildHarness(t)
	ptmx := startHarness(t, bin, "1")

	ptmx.Write([]byte("\x1b"))
	time.Sleep(300 * time.Millisecond) // past the 50ms arrow-key disambiguation window (a real, meaningful duration, not just scheduling slack)
	out := readWithTimeout(ptmx, testWindow)
	if !strings.Contains(out, "RESULT:-1") {
		t.Errorf("got %q, want it to contain RESULT:-1", out)
	}
}

func TestOutOfRangeDefaultIndexIsClamped(t *testing.T) {
	bin := buildHarness(t)
	ptmx := startHarness(t, bin, "99") // way past the 3 options - should clamp to index 2

	ptmx.Write([]byte("\r"))
	out := readWithTimeout(ptmx, testWindow)
	if !strings.Contains(out, "RESULT:2") {
		t.Errorf("got %q, want it to contain RESULT:2 (clamped from 99)", out)
	}
}
