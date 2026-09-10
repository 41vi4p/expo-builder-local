#pragma once
#include <optional>

#include "json.hpp"

namespace ebl {

struct ContainerStats {
  double cpuPercent = 0.0;
  double memUsedMb = 0.0;
  double memLimitMb = 0.0;
  double memPercent = 0.0;
};

/** Normalizes one frame of Docker's `/containers/{id}/stats` response into
 * ContainerStats - same formula the `docker` CLI itself uses for `docker stats`,
 * ported from orchestrator/src/docker/stats.ts's parseDockerStats (keep both in
 * sync by hand if either changes, same convention as detect.cpp/metrics.cpp's own
 * header comments). CPU%: (cpu_usage delta / system_cpu_usage delta) * online CPUs
 * * 100. Memory: raw usage minus the page-cache portion (matches what `docker
 * stats` shows, not the kernel's raw cgroup accounting) - handles both cgroup v1
 * (`stats.cache`) and cgroup v2 (`stats.inactive_file`) layouts. Pure, dependency-
 * free logic (no I/O) - kept in its own module specifically so cli/tests/ can
 * exercise it directly. Returns nullopt on a malformed/partial frame rather than
 * throwing - a live polling loop should just skip that tick and retry next time. */
std::optional<ContainerStats> parseDockerStats(const Json& raw);

}  // namespace ebl
