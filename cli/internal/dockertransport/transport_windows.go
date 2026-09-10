//go:build windows

package dockertransport

import (
	"context"
	"net"
	"net/http"

	"github.com/Microsoft/go-winio"
)

// New returns an *http.Client dialing Docker Desktop's named pipe for every
// request. socketPath is accepted for interface parity with the Unix build
// (and to preserve --docker-socket's meaning cross-platform in commands that
// still expose it there), but is otherwise unused - Windows always dials the
// fixed \\.\pipe\docker_engine pipe, matching http_client_win.cpp's own
// documented behavior. Replaces that file's entire hand-rolled HTTP/1.1
// framing over CreateFileW/ReadFile/WriteFile - go-winio's DialPipeContext
// gives a plain net.Conn, so net/http's own client handles all request/
// response/chunked-encoding framing exactly like the Unix build.
func New(_ string) *http.Client {
	return &http.Client{
		Transport: &http.Transport{
			DialContext: func(ctx context.Context, _, _ string) (net.Conn, error) {
				return winio.DialPipeContext(ctx, `\\.\pipe\docker_engine`)
			},
		},
	}
}

// BaseURL is the fake host every request is made against - the daemon itself
// ignores it (see New's doc comment).
const BaseURL = "http://docker"
