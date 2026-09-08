#pragma once
// Windows-only process-spawning helpers shared by native_toolchain.cpp (JDK/Android
// SDK/Node provisioning) and native_build.cpp (the native build engine itself) —
// see ../CLAUDE.md's native-engine section. UNVERIFIED ON REAL WINDOWS HARDWARE.
//
// Both callers need the same two things build-entrypoint.sh gets from `timeout
// --foreground --kill-after` (wall-clock) and its own pgrep/ps-based idle-CPU
// polling loop (idle detection) — reimplemented here on top of a Windows Job
// Object, which (unlike the bash version) can kill an entire process tree
// atomically via a single TerminateJobObject call, with no pgrp/monitor-suicide
// hazard to work around.
#include <functional>
#include <string>
#include <vector>

namespace ebl {

/** Sentinel exit code returned when a run*WithTimeout function had to kill the
 * process — matches build-entrypoint.sh's own use of 124 (the same value GNU
 * `timeout` itself returns) purely so error-message wording ("exceeded 300s") can
 * stay consistent between the Docker and native engines. Not read by the calling
 * process, so equality with the real `timeout` command's semantics does not
 * otherwise matter. */
constexpr int kProcessTimeoutExitCode = 124;

/** Runs `cmdLine` (a single, already-quoted Windows command-line string — see
 * quoteWindowsArg in native_process.cpp) in `workingDir`, with `envBlock` appended
 * to the current process's own environment (each entry "NAME=value"; empty means
 * "inherit unchanged"). Combined stdout+stderr is streamed to `onChunk` as it
 * arrives, from a background reader thread, mirroring
 * DockerClient::attachAndStream's shape/contract exactly, since native_build.cpp
 * reuses build.cpp's existing marker-line parser against this same callback shape.
 *
 * `stdinData`, if non-empty, is written to the child's stdin and the pipe is then
 * closed (EOF) — the child gets a genuinely separate stdin pipe instead of
 * inheriting this process's own console input. That's not just about feeding
 * canned answers: some interactive Java CLIs (sdkmanager's `--licenses` prompt
 * among them) call `System.console()`, which only returns non-null when *both*
 * stdin and stdout are a real, unredirected console — bypassing any piped input
 * entirely and prompting directly on the console instead, unanswerably, since
 * nothing is listening there. Forcing stdin to a real pipe (even an empty one)
 * guarantees `System.console()` returns null, so the tool falls back to reading
 * `System.in` normally. Left empty (the default), stdin is inherited exactly as
 * before — every other caller's behavior is unchanged.
 *
 * If the process is still running after `timeoutSeconds`, the whole process tree
 * is killed (Job Object + TerminateJobObject) and kProcessTimeoutExitCode is
 * returned. Throws std::runtime_error if the process can't even be spawned. */
int runProcessWithTimeout(const std::string& cmdLine, const std::string& workingDir,
                           const std::vector<std::string>& envBlock, int timeoutSeconds,
                           const std::function<void(const char*, size_t)>& onChunk,
                           const std::string& stdinData = "");

/** Same contract as runProcessWithTimeout, but idle-CPU-time based instead of a
 * flat wall-clock timeout — the direct analog of build-entrypoint.sh's
 * run_with_idle_timeout, for the two genuinely long, legitimately-bursty phases
 * (`eas build --local`'s internal Gradle invocation, and the direct `gradlew`
 * path). Polls the Job Object's cumulative CPU time (JobObjectBasicAccountingInformation
 * — summed across every process the job has ever contained, monotonically
 * increasing) every 5 seconds; kills the whole tree if that total hasn't advanced
 * for `idleSeconds`, or unconditionally once `maxSeconds` of wall-clock time has
 * elapsed, whichever comes first. */
int runProcessWithIdleTimeout(const std::string& cmdLine, const std::string& workingDir,
                               const std::vector<std::string>& envBlock, int idleSeconds, int maxSeconds,
                               const std::function<void(const char*, size_t)>& onChunk);

}  // namespace ebl
