//go:build !windows

// Package dockertransport builds an *http.Client that talks to the Docker
// Engine API over its local transport - a Unix domain socket
// (/var/run/docker.sock) on Linux/macOS, or the \\.\pipe\docker_engine named
// pipe on Windows (see transport_windows.go). Direct port of
// cli/src/http_client_unix.cpp/http_client_win.cpp, but net/http already
// speaks HTTP/1.1 chunked/Content-Length framing correctly, so - unlike the
// C++ version's Windows branch - there's no hand-rolled framing to write at
// all on either platform.
package dockertransport

import (
	"context"
	"net"
	"net/http"
)

// New returns an *http.Client dialing the given Unix socket path for every
// request. The URL host in requests is a formality (net/http requires one);
// Docker's daemon itself ignores it, same as the C++ version's fake
// "http://localhost" convention.
func New(socketPath string) *http.Client {
	return &http.Client{
		Transport: &http.Transport{
			DialContext: func(ctx context.Context, _, _ string) (net.Conn, error) {
				var d net.Dialer
				return d.DialContext(ctx, "unix", socketPath)
			},
		},
		// No client-wide Timeout: buffered calls set a per-request context
		// deadline (see dockerapi), and streaming calls (build/attach) can
		// legitimately run for many minutes - a blanket Timeout would cut
		// those off, same reasoning as the C++ version's
		// CURLOPT_TIMEOUT(0) for streamRequest.
	}
}

// BaseURL is the fake host every request is made against - the daemon itself
// ignores it (see New's doc comment).
const BaseURL = "http://docker"
