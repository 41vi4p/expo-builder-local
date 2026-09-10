package dockerapi

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"time"

	"github.com/41vi4p/expo-builder-local/cli/internal/dockerstats"
	"github.com/41vi4p/expo-builder-local/cli/internal/winpath"
)

type containerHostConfig struct {
	Binds      []string `json:"Binds"`
	AutoRemove bool     `json:"AutoRemove"`
}

type containerCreateBody struct {
	Image        string              `json:"Image"`
	Env          []string            `json:"Env"`
	Labels       map[string]string   `json:"Labels"`
	Tty          bool                `json:"Tty"`
	OpenStdin    bool                `json:"OpenStdin"`
	AttachStdout bool                `json:"AttachStdout"`
	AttachStderr bool                `json:"AttachStderr"`
	WorkingDir   string              `json:"WorkingDir"`
	HostConfig   containerHostConfig `json:"HostConfig"`
}

// CreateContainer creates (but does not start) a one-shot build container for
// params. buildUID/buildGID become BUILD_UID/BUILD_GID env vars, which
// docker/runner/docker-entrypoint.sh's UID/GID re-homing step consumes.
func (c *Client) CreateContainer(ctx context.Context, params BuildParams, runnerImage, gradleCacheVolume, npmCacheVolume string, buildUID, buildGID uint32) (string, error) {
	env := []string{
		"APP_DIR=" + ContainerAppDir,
		"ARTIFACT_TYPE=" + params.ArtifactType,
		"PROFILE=" + params.Profile,
		"ENGINE=" + params.Engine,
		"SIGNING_MODE=" + params.SigningMode,
		fmt.Sprintf("BUILD_UID=%d", buildUID),
		fmt.Sprintf("BUILD_GID=%d", buildGID),
	}
	if params.ExpoToken != "" {
		env = append(env, "EXPO_TOKEN="+params.ExpoToken)
	}

	// ToDockerBindPath is the identity function on non-Windows - this only
	// changes behavior when talking to Docker Desktop's daemon from a native
	// Windows ebl.exe, where a raw "D:\..." path means nothing to the
	// daemon's own Linux VM.
	binds := []string{
		winpath.ToDockerBindPath(params.AppPath) + ":" + ContainerAppDir,
		gradleCacheVolume + ":/cache/gradle",
		npmCacheVolume + ":/cache/npm",
	}

	if params.SigningMode == "release" && params.HasKeystore {
		containerPath := "/keystores/" + params.Keystore.Filename
		binds = append(binds, winpath.ToDockerBindPath(params.Keystore.HostPath)+":"+containerPath+":ro")
		keyPassword := params.Keystore.KeyPassword
		if keyPassword == "" {
			keyPassword = params.Keystore.StorePassword
		}
		env = append(env,
			"KEYSTORE_PATH="+containerPath,
			"KEYSTORE_PASSWORD="+params.Keystore.StorePassword,
			"KEY_ALIAS="+params.Keystore.KeyAlias,
			"KEY_PASSWORD="+keyPassword,
		)
	}

	body, err := json.Marshal(containerCreateBody{
		Image:        runnerImage,
		Env:          env,
		Labels:       map[string]string{AppPathLabel: params.AppPath},
		Tty:          true,
		OpenStdin:    false,
		AttachStdout: true,
		AttachStderr: true,
		WorkingDir:   ContainerAppDir,
		HostConfig:   containerHostConfig{Binds: binds, AutoRemove: false},
	})
	if err != nil {
		return "", err
	}

	status, respBody, err := c.do(ctx, http.MethodPost, "/containers/create", body, map[string]string{"Content-Type": "application/json"}, defaultTimeout)
	if err != nil {
		return "", err
	}
	if status != 201 {
		return "", fmt.Errorf("failed to create build container (HTTP %d): %s", status, dockerErrorMessage(respBody, ""))
	}

	var parsed struct {
		ID string `json:"Id"`
	}
	if err := json.Unmarshal(respBody, &parsed); err != nil {
		return "", err
	}
	return parsed.ID, nil
}

// StartContainer starts a previously created container.
func (c *Client) StartContainer(ctx context.Context, id string) error {
	status, body, err := c.do(ctx, http.MethodPost, "/containers/"+id+"/start", nil, nil, defaultTimeout)
	if err != nil {
		return err
	}
	if status != 204 && status != 304 {
		return fmt.Errorf("failed to start build container (HTTP %d): %s", status, body)
	}
	return nil
}

// AttachAndStream streams the container's combined stdout/stderr (Tty:true,
// so it's a raw, unmultiplexed byte stream) - onChunk fires as bytes arrive.
// Blocks until the container's output stream closes (which only happens once
// the container exits) - callers needing to do other work concurrently (e.g.
// polling stats, watching for cancellation) should run this on its own
// goroutine, same as the C++ CLI runs it on its own thread.
func (c *Client) AttachAndStream(ctx context.Context, id string, onChunk func([]byte)) error {
	path := "/containers/" + id + "/attach?stream=1&stdout=1&stderr=1"
	_, err := c.stream(ctx, http.MethodPost, path, nil, nil, onChunk)
	return err
}

// WaitContainer blocks until the container exits and returns its exit code.
// No timeout - a real build can run for many minutes.
func (c *Client) WaitContainer(ctx context.Context, id string) (int, error) {
	status, body, err := c.do(ctx, http.MethodPost, "/containers/"+id+"/wait", nil, nil, 0)
	if err != nil {
		return 0, err
	}
	if status != 200 {
		return 0, fmt.Errorf("failed waiting for the build container (HTTP %d): %s", status, body)
	}
	var parsed struct {
		StatusCode int `json:"StatusCode"`
	}
	if err := json.Unmarshal(body, &parsed); err != nil {
		return 0, err
	}
	return parsed.StatusCode, nil
}

// GetContainerStats takes a one-shot (not streaming) snapshot of the
// container's current CPU/memory usage. Meant to be polled periodically
// (e.g. once a second) from a live status view, not held open.
func (c *Client) GetContainerStats(ctx context.Context, id string) (dockerstats.ContainerStats, error) {
	status, body, err := c.do(ctx, http.MethodGet, "/containers/"+id+"/stats?stream=false", nil, nil, 10*time.Second)
	if err != nil {
		return dockerstats.ContainerStats{}, err
	}
	if status != 200 {
		return dockerstats.ContainerStats{}, fmt.Errorf("failed to fetch container stats (HTTP %d): %s", status, body)
	}
	stats, err := dockerstats.Parse(body)
	if err != nil {
		return dockerstats.ContainerStats{}, fmt.Errorf("malformed stats response from Docker: %w", err)
	}
	return stats, nil
}

// RemoveContainer force-removes a container.
func (c *Client) RemoveContainer(ctx context.Context, id string) error {
	status, body, err := c.do(ctx, http.MethodDelete, "/containers/"+id+"?force=1", nil, nil, defaultTimeout)
	if err != nil {
		return err
	}
	if status != 204 && status != 404 {
		return fmt.Errorf("failed to remove build container (HTTP %d): %s", status, body)
	}
	return nil
}

type dockerContainerSummary struct {
	ID    string `json:"Id"`
	State string `json:"State"`
}

// FindRunningBuildContainerByAppPath returns the id of an already-running
// build container for appPath, if any - every build container created by
// CreateContainer is labeled with its appPath, so this is how `ebl build`
// detects "a build for this project is already in flight" before launching a
// second one that would just contend with the first over the shared
// npm/gradle cache volumes.
func (c *Client) FindRunningBuildContainerByAppPath(ctx context.Context, appPath string) (string, bool, error) {
	filters, _ := json.Marshal(map[string][]string{"label": {AppPathLabel + "=" + appPath}})
	// No all=true: /containers/json defaults to running containers only,
	// which is exactly what "already in flight" means here.
	path := "/containers/json?filters=" + urlEncode(string(filters))
	status, body, err := c.do(ctx, http.MethodGet, path, nil, nil, defaultTimeout)
	if err != nil {
		return "", false, err
	}
	if status != 200 {
		return "", false, fmt.Errorf("failed to query Docker containers (HTTP %d): %s", status, body)
	}
	var containers []dockerContainerSummary
	if err := json.Unmarshal(body, &containers); err != nil {
		return "", false, err
	}
	if len(containers) == 0 {
		return "", false, nil
	}
	return containers[0].ID, true, nil
}

// ListBuildContainers returns every container (running or stopped) ebl
// itself created - labeled with AppPathLabel regardless of which project
// they were built for. Used by `ebl clean` to find its own leftover
// containers without guessing names.
func (c *Client) ListBuildContainers(ctx context.Context) ([]BuildContainerInfo, error) {
	filters, _ := json.Marshal(map[string][]string{"label": {AppPathLabel}})
	// all=1: unlike FindRunningBuildContainerByAppPath, this deliberately
	// includes stopped containers too - those are exactly what `ebl clean`
	// is looking for.
	path := "/containers/json?all=1&filters=" + urlEncode(string(filters))
	status, body, err := c.do(ctx, http.MethodGet, path, nil, nil, defaultTimeout)
	if err != nil {
		return nil, err
	}
	if status != 200 {
		return nil, fmt.Errorf("failed to list build containers (HTTP %d): %s", status, body)
	}
	var containers []dockerContainerSummary
	if err := json.Unmarshal(body, &containers); err != nil {
		return nil, err
	}
	result := make([]BuildContainerInfo, len(containers))
	for i, ct := range containers {
		result[i] = BuildContainerInfo{ID: ct.ID, State: ct.State}
	}
	return result, nil
}
