package hostprocess

import (
	"path/filepath"
	"runtime"
	"strings"
	"sync"
	"testing"
)

func collectChunks() (onChunk func([]byte), get func() string) {
	var mu sync.Mutex
	var buf strings.Builder
	onChunk = func(b []byte) {
		mu.Lock()
		defer mu.Unlock()
		buf.Write(b)
	}
	get = func() string {
		mu.Lock()
		defer mu.Unlock()
		return buf.String()
	}
	return
}

// sh turns a tiny POSIX-ish shell snippet into the argv for whatever shell
// the host actually has, so these tests run on both Linux/macOS and the
// Windows CI runner. Only the handful of constructs used below are
// translated (`;` -> `&`, `1>&2` is already valid in cmd) - it is not a
// general sh->cmd translator.
func sh(snippet string) []string {
	if runtime.GOOS == "windows" {
		return []string{"cmd", "/c", strings.ReplaceAll(snippet, ";", "&")}
	}
	return []string{"/bin/sh", "-c", snippet}
}

// printCwd is the shell command that prints the current working directory -
// `pwd` on POSIX, a bare `cd` on cmd.
func printCwd() []string {
	if runtime.GOOS == "windows" {
		return []string{"cmd", "/c", "cd"}
	}
	return []string{"/bin/sh", "-c", "pwd"}
}

// trickle prints "first", waits ~2s, then prints "second". The Windows sleep
// is `ping` rather than `timeout` because `timeout` aborts when stdin is
// redirected (as it is under `go test`).
func trickle() []string {
	if runtime.GOOS == "windows" {
		return []string{"cmd", "/c", "echo first& ping -n 3 127.0.0.1 >nul& echo second"}
	}
	return []string{"/bin/sh", "-c", "echo first; sleep 2; echo second"}
}

func TestStreamsCombinedStdoutAndStderr(t *testing.T) {
	onChunk, get := collectChunks()
	code, err := RunStreaming(sh("echo out-line; echo err-line 1>&2"), "", onChunk)
	if err != nil {
		t.Fatalf("RunStreaming: %v", err)
	}
	if code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
	got := get()
	if !strings.Contains(got, "out-line") || !strings.Contains(got, "err-line") {
		t.Errorf("got %q, want it to contain both out-line and err-line", got)
	}
}

func TestReturnsRealExitCode(t *testing.T) {
	onChunk, _ := collectChunks()
	code, err := RunStreaming(sh("exit 42"), "", onChunk)
	if err != nil {
		t.Fatalf("RunStreaming: %v", err)
	}
	if code != 42 {
		t.Errorf("code = %d, want 42", code)
	}
}

func TestRunsInTheGivenWorkingDirectory(t *testing.T) {
	dir := t.TempDir()
	onChunk, get := collectChunks()
	code, err := RunStreaming(printCwd(), dir, onChunk)
	if err != nil {
		t.Fatalf("RunStreaming: %v", err)
	}
	if code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
	// Resolve both sides through EvalSymlinks (macOS puts TempDir under a
	// /var -> /private/var symlink; Windows may report either the long or
	// 8.3-short form) and compare case-insensitively (Windows paths).
	want := dir
	if r, err := filepath.EvalSymlinks(dir); err == nil {
		want = r
	}
	got := strings.TrimSpace(get())
	if r, err := filepath.EvalSymlinks(got); err == nil {
		got = r
	}
	if !strings.EqualFold(got, want) {
		t.Errorf("pwd = %q, want %q", got, want)
	}
}

func TestNotFoundCommandReturnsNotFoundExitCode(t *testing.T) {
	onChunk, _ := collectChunks()
	code, err := RunStreaming([]string{"this-command-does-not-exist-anywhere-12345"}, "", onChunk)
	if err != nil {
		t.Fatalf("RunStreaming: %v", err)
	}
	if code != NotFoundExitCode {
		t.Errorf("code = %d, want %d", code, NotFoundExitCode)
	}
}

func TestEmptyArgsIsAnError(t *testing.T) {
	onChunk, _ := collectChunks()
	if _, err := RunStreaming(nil, "", onChunk); err == nil {
		t.Error("expected an error for an empty args slice")
	}
}

func TestOutputArrivesLiveNotJustAtTheEnd(t *testing.T) {
	// A slow-trickling command whose output is checked before it exits -
	// proves this streams rather than buffering until completion, the whole
	// point of this package over a plain exec.Command().CombinedOutput().
	onChunk, get := collectChunks()
	firstChunkSeen := make(chan struct{}, 1)
	wrapped := func(b []byte) {
		onChunk(b)
		select {
		case firstChunkSeen <- struct{}{}:
		default:
		}
	}

	done := make(chan struct{})
	go func() {
		defer close(done)
		RunStreaming(trickle(), "", wrapped)
	}()

	<-firstChunkSeen
	if got := get(); !strings.Contains(got, "first") {
		t.Errorf("expected 'first' to have arrived already, got %q", got)
	}
	if strings.Contains(get(), "second") {
		t.Error("'second' should not have arrived yet - the command is still sleeping")
	}
	<-done
}
