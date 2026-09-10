// Package dockerstats normalizes one frame of Docker's
// /containers/{id}/stats response into CPU%/memory - the same formula the
// `docker` CLI itself uses for `docker stats`, ported from
// orchestrator/src/docker/stats.ts (keep in sync by hand if either changes).
// Direct port of cli/src/docker_stats.cpp.
package dockerstats

import (
	"encoding/json"
	"errors"
)

// ContainerStats is the normalized result of one stats frame.
type ContainerStats struct {
	CPUPercent float64
	MemUsedMB  float64
	MemLimitMB float64
	MemPercent float64
}

type cpuUsage struct {
	TotalUsage  float64   `json:"total_usage"`
	PercpuUsage []float64 `json:"percpu_usage"`
}

type cpuStatsFrame struct {
	CPUUsage       cpuUsage `json:"cpu_usage"`
	SystemCPUUsage float64  `json:"system_cpu_usage"`
	OnlineCPUs     float64  `json:"online_cpus"`
}

type memoryStatsDetail struct {
	Cache        *float64 `json:"cache"`
	InactiveFile *float64 `json:"inactive_file"`
}

type memoryStatsFrame struct {
	Usage float64           `json:"usage"`
	Limit float64           `json:"limit"`
	Stats memoryStatsDetail `json:"stats"`
}

// rawFrame mirrors the subset of Docker's stats JSON this package cares about.
// cpu_stats/precpu_stats are pointers specifically so a missing key is
// distinguishable from a present-but-empty object - Parse treats a missing key
// as a malformed frame (matches the C++ version's isObject() checks).
type rawFrame struct {
	CPUStats    *cpuStatsFrame   `json:"cpu_stats"`
	PrecpuStats *cpuStatsFrame   `json:"precpu_stats"`
	MemoryStats memoryStatsFrame `json:"memory_stats"`
}

const mb = 1024.0 * 1024.0

// ErrMalformedFrame is returned when the frame is missing cpu_stats/precpu_stats
// - a live polling loop should just skip that tick and retry next time, same as
// the C++ version's std::nullopt return.
var ErrMalformedFrame = errors.New("dockerstats: frame missing cpu_stats/precpu_stats")

// Parse decodes and normalizes one raw JSON stats frame. Docker's CPU%:
// (cpu_usage delta / system_cpu_usage delta) * online CPUs * 100. Memory: raw
// usage minus the page-cache portion (matches what `docker stats` shows, not
// the kernel's raw cgroup accounting) - handles both cgroup v1 (`stats.cache`)
// and cgroup v2 (`stats.inactive_file`) layouts.
func Parse(data []byte) (ContainerStats, error) {
	var raw rawFrame
	if err := json.Unmarshal(data, &raw); err != nil {
		return ContainerStats{}, err
	}
	if raw.CPUStats == nil || raw.PrecpuStats == nil {
		return ContainerStats{}, ErrMalformedFrame
	}

	cpuDelta := raw.CPUStats.CPUUsage.TotalUsage - raw.PrecpuStats.CPUUsage.TotalUsage
	systemDelta := raw.CPUStats.SystemCPUUsage - raw.PrecpuStats.SystemCPUUsage

	onlineCPUs := raw.CPUStats.OnlineCPUs
	if onlineCPUs <= 0 {
		if n := len(raw.CPUStats.CPUUsage.PercpuUsage); n > 0 {
			onlineCPUs = float64(n)
		} else {
			onlineCPUs = 1.0
		}
	}

	var result ContainerStats
	if systemDelta > 0 && cpuDelta > 0 {
		result.CPUPercent = (cpuDelta / systemDelta) * onlineCPUs * 100.0
	}

	cache := 0.0
	if raw.MemoryStats.Stats.Cache != nil {
		cache = *raw.MemoryStats.Stats.Cache
	} else if raw.MemoryStats.Stats.InactiveFile != nil {
		cache = *raw.MemoryStats.Stats.InactiveFile
	}
	memUsage := raw.MemoryStats.Usage - cache
	if memUsage < 0 {
		memUsage = 0
	}
	memLimit := raw.MemoryStats.Limit
	if memLimit == 0 {
		memLimit = 1.0
	}

	result.MemUsedMB = memUsage / mb
	result.MemLimitMB = memLimit / mb
	if memLimit > 0 {
		result.MemPercent = (memUsage / memLimit) * 100.0
	}

	return result, nil
}
