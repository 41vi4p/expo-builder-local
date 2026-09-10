package dockerapi

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
)

// FindContainerIDByName returns (id, true, nil) if a container with this name
// exists (running or stopped), or ("", false, nil) if not.
func (c *Client) FindContainerIDByName(ctx context.Context, name string) (string, bool, error) {
	status, body, err := c.do(ctx, http.MethodGet, "/containers/"+name+"/json", nil, nil, defaultTimeout)
	if err != nil {
		return "", false, err
	}
	if status == 404 {
		return "", false, nil
	}
	if status != 200 {
		return "", false, fmt.Errorf("failed to inspect container %q (HTTP %d): %s", name, status, body)
	}
	var parsed struct {
		ID string `json:"Id"`
	}
	if err := json.Unmarshal(body, &parsed); err != nil {
		return "", false, err
	}
	return parsed.ID, true, nil
}

// IsContainerRunning reports whether a container is currently running. Never
// errors - any failure (container gone, malformed response) is treated as
// "not running".
func (c *Client) IsContainerRunning(ctx context.Context, id string) bool {
	status, body, err := c.do(ctx, http.MethodGet, "/containers/"+id+"/json", nil, nil, defaultTimeout)
	if err != nil || status != 200 {
		return false
	}
	var parsed struct {
		State struct {
			Running bool `json:"Running"`
		} `json:"State"`
	}
	if json.Unmarshal(body, &parsed) != nil {
		return false
	}
	return parsed.State.Running
}

// RemoveContainerByName force-removes the named container if it exists;
// a no-op otherwise.
func (c *Client) RemoveContainerByName(ctx context.Context, name string) error {
	id, found, err := c.FindContainerIDByName(ctx, name)
	if err != nil {
		return err
	}
	if !found {
		return nil
	}
	return c.RemoveContainer(ctx, id)
}

type restartPolicy struct {
	Name string `json:"Name"`
}

type portBindingEntry struct {
	HostIP   string `json:"HostIp"`
	HostPort string `json:"HostPort"`
}

type serviceHostConfig struct {
	Binds         []string                      `json:"Binds"`
	PortBindings  map[string][]portBindingEntry `json:"PortBindings"`
	RestartPolicy restartPolicy                 `json:"RestartPolicy"`
	NetworkMode   string                        `json:"NetworkMode,omitempty"`
}

type serviceCreateBody struct {
	Image        string              `json:"Image"`
	Env          []string            `json:"Env"`
	ExposedPorts map[string]struct{} `json:"ExposedPorts"`
	HostConfig   serviceHostConfig   `json:"HostConfig"`
}

// CreateServiceContainer creates (but does not start) a detached,
// auto-restarting service container. If a container with this name already
// exists, it's removed first (fresh config on every `ebl start`, rather than
// silently reusing stale settings).
func (c *Client) CreateServiceContainer(ctx context.Context, spec ServiceContainerSpec) (string, error) {
	if err := c.RemoveContainerByName(ctx, spec.Name); err != nil {
		return "", err
	}
	if spec.Network != "" {
		if err := c.EnsureNetwork(ctx, spec.Network); err != nil {
			return "", err
		}
	}

	exposedPorts := map[string]struct{}{}
	portBindings := map[string][]portBindingEntry{}
	for _, pb := range spec.PortBindings {
		exposedPorts[pb.ContainerPort] = struct{}{}
		portBindings[pb.ContainerPort] = []portBindingEntry{{HostIP: "127.0.0.1", HostPort: pb.HostPort}}
	}

	binds := spec.Binds
	if binds == nil {
		binds = []string{}
	}
	env := spec.Env
	if env == nil {
		env = []string{}
	}

	body, err := json.Marshal(serviceCreateBody{
		Image:        spec.Image,
		Env:          env,
		ExposedPorts: exposedPorts,
		HostConfig: serviceHostConfig{
			Binds:         binds,
			PortBindings:  portBindings,
			RestartPolicy: restartPolicy{Name: "unless-stopped"},
			NetworkMode:   spec.Network,
		},
	})
	if err != nil {
		return "", err
	}

	path := "/containers/create?name=" + urlEncode(spec.Name)
	status, respBody, err := c.do(ctx, http.MethodPost, path, body, map[string]string{"Content-Type": "application/json"}, defaultTimeout)
	if err != nil {
		return "", err
	}
	if status != 201 {
		return "", fmt.Errorf("failed to create %q (HTTP %d): %s", spec.Name, status, dockerErrorMessage(respBody, ""))
	}

	var parsed struct {
		ID string `json:"Id"`
	}
	if err := json.Unmarshal(respBody, &parsed); err != nil {
		return "", err
	}
	return parsed.ID, nil
}
