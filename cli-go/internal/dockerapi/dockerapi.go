// Package dockerapi implements the high-level Docker Engine API operations
// the CLI needs, on top of dockertransport's Unix-socket/named-pipe HTTP
// client. Direct port of cli/src/docker_client.cpp - mirrors what
// orchestrator/src/docker/runner.ts does for the GUI/orchestrator, but
// talking to the daemon directly instead of via dockerode.
package dockerapi

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/dockertransport"
	"github.com/41vi4p/expo-builder-local/cli/internal/tarctx"
)

// ContainerAppDir is where a build container sees the bind-mounted project
// root (matches docker/runner/build-entrypoint.sh's default APP_DIR, and
// orchestrator/src/docker/runner.ts's CONTAINER_APP_DIR) - paths the
// container reports back (e.g. the @@ARTIFACT: marker) are rooted here, not
// at the real host path, so callers must translate before touching the host
// filesystem.
const ContainerAppDir = "/work/app"

// AppPathLabel is the Docker label key every build container is tagged with
// (value: the host appPath being built) - lets
// FindRunningBuildContainerByAppPath detect a build already in flight for a
// given project without needing a deterministic container name.
const AppPathLabel = "com.expo-builder-local.app-path"

const defaultTimeout = 120 * time.Second

// KeystoreConfig describes a release-signing keystore bind-mounted into the
// build container.
type KeystoreConfig struct {
	HostPath      string
	Filename      string
	StorePassword string
	KeyAlias      string
	KeyPassword   string
}

// BuildParams describes one build container's configuration.
type BuildParams struct {
	AppPath      string // absolute host path to the Expo project root
	ArtifactType string // apk | aab
	Profile      string
	Engine       string // auto | gradle | eas
	SigningMode  string // debug | release
	ExpoToken    string // optional
	HasKeystore  bool
	Keystore     KeystoreConfig
}

// PortBinding is one container-port -> host-port mapping, published on
// 127.0.0.1 only.
type PortBinding struct {
	ContainerPort string // e.g. "4001/tcp"
	HostPort      string // e.g. "4001"
}

// ServiceContainerSpec describes a long-running service container
// (orchestrator or web), as opposed to the one-shot, disposable build
// containers BuildParams describes.
type ServiceContainerSpec struct {
	Name         string // deterministic name, e.g. "ebl-orchestrator" - used for lookup/removal
	Image        string
	Env          []string
	Binds        []string // "host-path:container-path[:ro]"
	Network      string   // network name to attach to (created if missing)
	PortBindings []PortBinding
}

// BuildContainerInfo is one entry from ListBuildContainers.
type BuildContainerInfo struct {
	ID    string
	State string // "running", "exited", ...
}

// Client talks to the Docker Engine API over its local transport.
type Client struct {
	http    *http.Client
	baseURL string
}

// New returns a Client dialing the Docker daemon over socketPath (a Unix
// socket path on Linux/macOS; accepted but unused on Windows, which always
// dials Docker Desktop's fixed named pipe - see dockertransport).
func New(socketPath string) *Client {
	return &Client{http: dockertransport.New(socketPath), baseURL: dockertransport.BaseURL}
}

// urlEncode percent-encodes everything except unreserved characters
// (RFC 3986) - used for query-string values like the image tag or a JSON
// filters blob. Deliberately not net/url.QueryEscape, which encodes spaces
// as '+' rather than %20 and isn't guaranteed byte-identical to what the
// C++ CLI has always sent Docker's daemon.
func urlEncode(value string) string {
	const hex = "0123456789ABCDEF"
	var out strings.Builder
	out.Grow(len(value) * 3)
	for i := 0; i < len(value); i++ {
		c := value[i]
		if (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '-' || c == '_' || c == '.' || c == '~' {
			out.WriteByte(c)
		} else {
			out.WriteByte('%')
			out.WriteByte(hex[(c>>4)&0xF])
			out.WriteByte(hex[c&0xF])
		}
	}
	return out.String()
}

// do performs a buffered request, for calls with a bounded response size. A
// zero timeout means no deadline - used for /containers/{id}/wait, which
// blocks until the build finishes.
func (c *Client) do(ctx context.Context, method, path string, body []byte, headers map[string]string, timeout time.Duration) (int, []byte, error) {
	if timeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, timeout)
		defer cancel()
	}

	var bodyReader io.Reader
	if len(body) > 0 {
		bodyReader = bytes.NewReader(body)
	}
	req, err := http.NewRequestWithContext(ctx, method, c.baseURL+path, bodyReader)
	if err != nil {
		return 0, nil, err
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}

	resp, err := c.http.Do(req)
	if err != nil {
		return 0, nil, fmt.Errorf("request to the Docker daemon failed: %w (is Docker running, and is the socket path correct?)", err)
	}
	defer resp.Body.Close()

	respBody, err := io.ReadAll(resp.Body)
	if err != nil {
		return 0, nil, fmt.Errorf("reading the Docker daemon's response failed: %w", err)
	}
	return resp.StatusCode, respBody, nil
}

// stream performs a request whose response body may be long-lived (build,
// pull, attach, wait), invoking onChunk as bytes arrive rather than buffering
// the whole response. No timeout is ever applied here - builds and running
// containers can legitimately take many minutes; cancellation is entirely up
// to ctx (a caller wanting a Ctrl-C-cancellable request passes a
// context.Context tied to that signal).
func (c *Client) stream(ctx context.Context, method, path string, body []byte, headers map[string]string, onChunk func([]byte)) (int, error) {
	var bodyReader io.Reader
	if len(body) > 0 {
		bodyReader = bytes.NewReader(body)
	}
	req, err := http.NewRequestWithContext(ctx, method, c.baseURL+path, bodyReader)
	if err != nil {
		return 0, err
	}
	for k, v := range headers {
		req.Header.Set(k, v)
	}

	resp, err := c.http.Do(req)
	if err != nil {
		return 0, fmt.Errorf("streaming request to the Docker daemon failed: %w", err)
	}
	defer resp.Body.Close()

	buf := make([]byte, 32*1024)
	for {
		n, readErr := resp.Body.Read(buf)
		if n > 0 {
			onChunk(buf[:n])
		}
		if readErr == io.EOF {
			break
		}
		if readErr != nil {
			return resp.StatusCode, fmt.Errorf("streaming request to the Docker daemon failed: %w", readErr)
		}
	}
	return resp.StatusCode, nil
}

func dockerErrorMessage(body []byte, fallback string) string {
	var parsed struct {
		Message string `json:"message"`
	}
	if json.Unmarshal(body, &parsed) == nil && parsed.Message != "" {
		return parsed.Message
	}
	if fallback != "" {
		return fallback
	}
	return string(body)
}

// Ping reports whether the Docker daemon is reachable at all over the
// configured socket - never errors, used by `ebl setup` to distinguish
// "not installed"/"not running" from a real error.
func (c *Client) Ping(ctx context.Context) bool {
	status, _, err := c.do(ctx, http.MethodGet, "/_ping", nil, nil, defaultTimeout)
	return err == nil && status == 200
}

// ImageExists reports whether an image with this exact tag is present
// locally.
func (c *Client) ImageExists(ctx context.Context, tag string) (bool, error) {
	filters, _ := json.Marshal(map[string][]string{"reference": {tag}})
	path := "/images/json?filters=" + urlEncode(string(filters))
	status, body, err := c.do(ctx, http.MethodGet, path, nil, nil, defaultTimeout)
	if err != nil {
		return false, err
	}
	if status != 200 {
		return false, fmt.Errorf("failed to query Docker images (HTTP %d): %s", status, body)
	}
	var images []json.RawMessage
	if err := json.Unmarshal(body, &images); err != nil {
		return false, err
	}
	return len(images) > 0, nil
}

// RemoveImage deletes an image by tag; a no-op (not an error) if it doesn't
// exist.
func (c *Client) RemoveImage(ctx context.Context, tag string) error {
	status, body, err := c.do(ctx, http.MethodDelete, "/images/"+urlEncode(tag), nil, nil, defaultTimeout)
	if err != nil {
		return err
	}
	if status != 200 && status != 404 {
		return fmt.Errorf("failed to remove image %q (HTTP %d): %s", tag, status, body)
	}
	return nil
}

type buildStreamEvent struct {
	Stream         string `json:"stream"`
	Status         string `json:"status"`
	Progress       string `json:"progress"`
	Error          string `json:"error"`
	ID             string `json:"id"`
	ProgressDetail *struct {
		Current int64 `json:"current"`
		Total   int64 `json:"total"`
	} `json:"progressDetail"`
}

// BuildImage tars contextDir and POSTs it to /build, invoking onLog for each
// line of build output. Returns an error if the daemon reports one.
//
// noCache forces Docker to ignore its build cache entirely (nocache=1) and
// re-pull the FROM base image even if one matching it is already local
// (pull=1) - without it, a `RUN npm install -g eas-cli@latest`-style
// instruction only ever re-resolves "latest" the very first time that layer
// is built. `ebl update` passes true specifically to defeat that; every
// other caller leaves it false so day-to-day builds still benefit from layer
// caching.
func (c *Client) BuildImage(ctx context.Context, contextDir, tag string, onLog func(string), noCache bool) error {
	tar, err := tarctx.FromDirectory(contextDir)
	if err != nil {
		return err
	}

	path := "/build?t=" + urlEncode(tag) + "&rm=1"
	if noCache {
		path += "&nocache=1&pull=1"
	}

	var firstError string
	var residual bytes.Buffer
	onChunk := func(chunk []byte) {
		residual.Write(chunk)
		for {
			b := residual.Bytes()
			idx := bytes.IndexByte(b, '\n')
			if idx < 0 {
				break
			}
			line := string(b[:idx])
			residual.Next(idx + 1)
			if line == "" {
				continue
			}

			var event buildStreamEvent
			if err := json.Unmarshal([]byte(line), &event); err != nil {
				onLog(line) // not a JSON line (shouldn't normally happen) - show it verbatim
				continue
			}
			switch {
			case event.Stream != "":
				onLog(event.Stream)
			case event.Error != "":
				firstError = event.Error
			case event.Status != "":
				status := event.Status
				if event.Progress != "" {
					status += " " + event.Progress
				}
				onLog(status + "\n")
			}
		}
	}

	status, err := c.stream(ctx, http.MethodPost, path, tar, map[string]string{"Content-Type": "application/x-tar"}, onChunk)
	if err != nil {
		return err
	}
	if firstError != "" {
		return fmt.Errorf("Docker build failed: %s", firstError)
	}
	if status != 200 {
		return fmt.Errorf("Docker build request failed (HTTP %d)", status)
	}
	return nil
}

// dockerCLIHumanSize formats bytes the way `docker pull` does: 1000-based
// with ~4 significant digits (e.g. "892.7kB", "5.545MB"), mirroring
// docker/pkg/units.CustomSize.
func dockerCLIHumanSize(bytes float64) string {
	units := []string{"B", "kB", "MB", "GB", "TB", "PB"}
	unit := 0
	value := bytes
	for value >= 1000.0 && unit+1 < len(units) {
		value /= 1000.0
		unit++
	}
	return strconv.FormatFloat(value, 'g', 4, 64) + units[unit]
}

// dockerCLIProgressBar renders a `docker pull`-style ASCII bar: "[===>   ]".
// Fixed width rather than measured against the real terminal width (as the
// actual `docker` CLI does) - simpler, and every caller already
// truncates/redraws the surrounding line itself.
func dockerCLIProgressBar(current, total int64, width int) string {
	frac := 0.0
	if total > 0 {
		frac = float64(current) / float64(total)
	}
	if frac < 0 {
		frac = 0
	}
	if frac > 1 {
		frac = 1
	}
	filled := int(frac * float64(width))
	bar := []byte(strings.Repeat(" ", width))
	for i := 0; i < filled && i < width; i++ {
		bar[i] = '='
	}
	if filled > 0 && filled < width {
		bar[filled] = '>'
	}
	return "[" + string(bar) + "]"
}

// PullEvent is one status update from PullImage.
type PullEvent struct {
	ID       string
	Status   string
	Progress string
}

// PullImage pulls tag from its registry (Docker Hub unless the tag names
// another registry host), invoking onEvent for each status line. Returns an
// error on failure - e.g. the tag doesn't exist, or there's no network
// access.
func (c *Client) PullImage(ctx context.Context, tag string, onEvent func(PullEvent)) error {
	// Docker's pull endpoint takes the repo and tag as separate query params.
	// Split on the last ':' - but only if nothing after it looks like a "/" (a
	// bare "registry:port/name" host has no tag, and defaults to "latest").
	repo := tag
	imageTag := "latest"
	if colon := strings.LastIndexByte(tag, ':'); colon != -1 && !strings.Contains(tag[colon:], "/") {
		repo = tag[:colon]
		imageTag = tag[colon+1:]
	}

	path := "/images/create?fromImage=" + urlEncode(repo) + "&tag=" + urlEncode(imageTag)

	var firstError string
	var residual bytes.Buffer
	onChunk := func(chunk []byte) {
		residual.Write(chunk)
		for {
			b := residual.Bytes()
			idx := bytes.IndexByte(b, '\n')
			if idx < 0 {
				break
			}
			line := string(b[:idx])
			residual.Next(idx + 1)
			if line == "" {
				continue
			}

			var event buildStreamEvent
			if err := json.Unmarshal([]byte(line), &event); err != nil {
				continue
			}
			if event.Error != "" {
				firstError = event.Error
				continue
			}
			if event.Status == "" {
				continue
			}
			// The daemon's own /images/create stream never actually includes a
			// pre-rendered "progress" bar string (that's the real `docker`
			// CLI's own client-side rendering) - only "progressDetail":
			// {current, total} byte counts, present on
			// "Downloading"/"Extracting" events. Build the bar ourselves from
			// those; keep the "progress" key too in case some future engine
			// version does send one.
			progress := event.Progress
			if progress == "" && event.ProgressDetail != nil && event.ProgressDetail.Total > 0 {
				d := event.ProgressDetail
				progress = dockerCLIProgressBar(d.Current, d.Total, 30) + "  " +
					dockerCLIHumanSize(float64(d.Current)) + "/" + dockerCLIHumanSize(float64(d.Total))
			}
			onEvent(PullEvent{ID: event.ID, Status: event.Status, Progress: progress})
		}
	}

	status, err := c.stream(ctx, http.MethodPost, path, nil, nil, onChunk)
	if err != nil {
		return err
	}
	if firstError != "" {
		return fmt.Errorf("failed to pull %s: %s", tag, firstError)
	}
	if status != 200 {
		return fmt.Errorf("pull request for %s failed (HTTP %d)", tag, status)
	}
	return nil
}

// EnsureVolume creates a Docker volume if it doesn't already exist.
// Idempotent: Docker returns the existing volume as-is if the name already
// exists, since no driver-specific options are ever passed that could
// conflict.
func (c *Client) EnsureVolume(ctx context.Context, name string) error {
	body, _ := json.Marshal(map[string]string{"Name": name})
	status, respBody, err := c.do(ctx, http.MethodPost, "/volumes/create", body, map[string]string{"Content-Type": "application/json"}, defaultTimeout)
	if err != nil {
		return err
	}
	if status != 201 && status != 200 {
		return fmt.Errorf("failed to create Docker volume %q (HTTP %d): %s", name, status, respBody)
	}
	return nil
}

// RemoveVolume deletes a volume by name; a no-op (not an error) if it
// doesn't exist. Fails with Docker's own error if the volume is still in use
// by a container.
func (c *Client) RemoveVolume(ctx context.Context, name string) error {
	status, body, err := c.do(ctx, http.MethodDelete, "/volumes/"+urlEncode(name), nil, nil, defaultTimeout)
	if err != nil {
		return err
	}
	if status != 204 && status != 404 {
		return fmt.Errorf("failed to remove volume %q (HTTP %d): %s", name, status, body)
	}
	return nil
}

// EnsureNetwork creates a Docker network if it doesn't already exist. Unlike
// volumes, Docker's network create is NOT idempotent (a duplicate name is a
// 409 Conflict), so this checks first.
func (c *Client) EnsureNetwork(ctx context.Context, name string) error {
	status, _, err := c.do(ctx, http.MethodGet, "/networks/"+name, nil, nil, defaultTimeout)
	if err != nil {
		return err
	}
	if status == 200 {
		return nil
	}

	body, _ := json.Marshal(map[string]any{"Name": name, "CheckDuplicate": true})
	status, respBody, err := c.do(ctx, http.MethodPost, "/networks/create", body, map[string]string{"Content-Type": "application/json"}, defaultTimeout)
	if err != nil {
		return err
	}
	if status != 201 {
		return fmt.Errorf("failed to create Docker network %q (HTTP %d): %s", name, status, respBody)
	}
	return nil
}
