//go:build windows

package commands

// buildUIDGID: see cli/src/commands/start.cpp's HOST_UID/HOST_GID comment -
// same placeholder-pending-verification reasoning applies to the build
// container's UID/GID re-homing. Windows has no POSIX uid/gid to report;
// this is what docker/runner/docker-entrypoint.sh's UID/GID re-homing step
// consumes, and the actual value Docker Desktop's file-sharing layer
// presents bind-mounted host files under hasn't been confirmed against a
// real install yet.
func buildUIDGID() (uint32, uint32) {
	return 1000, 1000
}
