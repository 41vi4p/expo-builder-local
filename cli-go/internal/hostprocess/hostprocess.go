// Package hostprocess runs a command directly on the host (no Docker, no
// shell), streaming its combined stdout+stderr live. Direct port of
// cli/src/host_process.hpp/.cpp - a large simplification over that file's
// manual fork/exec/pipe (POSIX) and CreateProcess/CreatePipe (Windows)
// branches, since os/exec already does all of that portably.
package hostprocess

import (
	"errors"
	"io"
	"os/exec"
)

// NotFoundExitCode is returned by RunStreaming when the command couldn't be
// found on PATH - the same convention every POSIX shell uses for "command
// not found" (a failed execvp exits 127), which cli/src/commands/create.cpp
// (and its Go port) checks for specifically to give a clearer message than a
// raw exit code.
const NotFoundExitCode = 127

// RunStreaming runs args[0] with args[1:], with its working directory set to
// workingDir, streaming combined stdout+stderr to onChunk live as it
// arrives - the same shape as dockerapi.Client.AttachAndStream's callback,
// so callers can reuse familiar line-buffering logic against it. Blocks
// until the process exits.
//
// Returns the process's real exit code, or -1 if it couldn't even be spawned
// for a reason other than "not found" (onChunk is never called in that
// case). Unlike the C++ version's fork+execvp (where a missing executable is
// only discovered inside the forked child, surfacing as a real exit code
// 127), Go's os/exec resolves the executable via PATH before spawning
// anything - a lookup failure returns an error from Start() with no process
// ever created. RunStreaming detects that specific case and reports
// NotFoundExitCode anyway, so callers can check for it the same way
// regardless of which OS/implementation is underneath.
func RunStreaming(args []string, workingDir string, onChunk func([]byte)) (int, error) {
	if len(args) == 0 {
		return -1, errors.New("hostprocess: no command given")
	}

	cmd := exec.Command(args[0], args[1:]...)
	cmd.Dir = workingDir

	// An io.Pipe, not an OS pipe: since Stdout/Stderr aren't *os.File, the
	// exec package itself spawns the goroutines that copy the child's real
	// output into pw - cmd.Wait() blocks until those finish, which is
	// exactly the signal this needs to know no more writes are coming before
	// closing pw below.
	pr, pw := io.Pipe()
	cmd.Stdout = pw
	cmd.Stderr = pw

	if err := cmd.Start(); err != nil {
		pr.Close()
		pw.Close()
		if errors.Is(err, exec.ErrNotFound) {
			return NotFoundExitCode, nil
		}
		return -1, err
	}

	done := make(chan struct{})
	go func() {
		defer close(done)
		buf := make([]byte, 4096)
		for {
			n, err := pr.Read(buf)
			if n > 0 {
				onChunk(buf[:n])
			}
			if err != nil {
				return
			}
		}
	}()

	waitErr := cmd.Wait()
	pw.Close() // no more writes will happen now - unblocks pr.Read with a clean EOF
	<-done
	pr.Close()

	if waitErr == nil {
		return 0, nil
	}
	var exitErr *exec.ExitError
	if errors.As(waitErr, &exitErr) {
		return exitErr.ExitCode(), nil
	}
	return -1, waitErr
}
