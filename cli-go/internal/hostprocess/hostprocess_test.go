package hostprocess

import (
	"os"
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

func TestStreamsCombinedStdoutAndStderr(t *testing.T) {
	onChunk, get := collectChunks()
	code, err := RunStreaming([]string{"/bin/sh", "-c", "echo out-line; echo err-line 1>&2"}, "", onChunk)
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
	code, err := RunStreaming([]string{"/bin/sh", "-c", "exit 42"}, "", onChunk)
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
	code, err := RunStreaming([]string{"/bin/sh", "-c", "pwd"}, dir, onChunk)
	if err != nil {
		t.Fatalf("RunStreaming: %v", err)
	}
	if code != 0 {
		t.Errorf("code = %d, want 0", code)
	}
	resolvedDir, err := os.Readlink(dir)
	if err != nil {
		resolvedDir = dir // dir wasn't a symlink itself; use as-is
	}
	got := strings.TrimSpace(get())
	if got != dir && got != resolvedDir {
		t.Errorf("pwd = %q, want %q (or its resolved form %q)", got, dir, resolvedDir)
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
		RunStreaming([]string{"/bin/sh", "-c", "echo first; sleep 2; echo second"}, "", wrapped)
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
