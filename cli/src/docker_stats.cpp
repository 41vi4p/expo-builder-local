#include "docker_stats.hpp"

#include <algorithm>

namespace ebl {

std::optional<ContainerStats> parseDockerStats(const Json& raw) {
  if (!raw.isObject()) return std::nullopt;

  Json cpuStats = raw.get("cpu_stats");
  Json precpuStats = raw.get("precpu_stats");
  if (!cpuStats.isObject() || !precpuStats.isObject()) return std::nullopt;

  double totalUsage = cpuStats.get("cpu_usage").get("total_usage").asDouble(0.0);
  double preTotalUsage = precpuStats.get("cpu_usage").get("total_usage").asDouble(0.0);
  double systemUsage = cpuStats.get("system_cpu_usage").asDouble(0.0);
  double preSystemUsage = precpuStats.get("system_cpu_usage").asDouble(0.0);

  double cpuDelta = totalUsage - preTotalUsage;
  double systemDelta = systemUsage - preSystemUsage;

  double onlineCpus = cpuStats.get("online_cpus").asDouble(0.0);
  if (onlineCpus <= 0.0) {
    Json percpu = cpuStats.get("cpu_usage").get("percpu_usage");
    onlineCpus = percpu.isArray() && percpu.size() > 0 ? static_cast<double>(percpu.size()) : 1.0;
  }

  ContainerStats result;
  result.cpuPercent = (systemDelta > 0 && cpuDelta > 0) ? (cpuDelta / systemDelta) * onlineCpus * 100.0 : 0.0;

  Json memoryStats = raw.get("memory_stats");
  double memUsageRaw = memoryStats.get("usage").asDouble(0.0);
  Json memStatsDetail = memoryStats.get("stats");
  double cache = memStatsDetail.get("cache").asDouble(-1.0);
  if (cache < 0.0) cache = memStatsDetail.get("inactive_file").asDouble(0.0);
  double memUsage = std::max(0.0, memUsageRaw - cache);
  double memLimit = memoryStats.get("limit").asDouble(1.0);

  constexpr double kMb = 1024.0 * 1024.0;
  result.memUsedMb = memUsage / kMb;
  result.memLimitMb = memLimit / kMb;
  result.memPercent = memLimit > 0 ? (memUsage / memLimit) * 100.0 : 0.0;

  return result;
}

}  // namespace ebl
