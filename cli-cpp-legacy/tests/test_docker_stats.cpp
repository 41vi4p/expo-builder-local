#include "../src/docker_stats.hpp"
#include "test_framework.hpp"

using ebl::Json;
using ebl::parseDockerStats;

namespace {

// A realistic-shaped Docker /containers/{id}/stats frame, with cpu_stats numbers
// chosen so the expected CPU% comes out to a round number: cpuDelta=2, systemDelta=8,
// onlineCpus=4 -> (2/8)*4*100 = 100%.
const char* kSampleFrame = R"({
  "cpu_stats": {
    "cpu_usage": { "total_usage": 1002, "percpu_usage": [1,2,3,4] },
    "system_cpu_usage": 10008,
    "online_cpus": 4
  },
  "precpu_stats": {
    "cpu_usage": { "total_usage": 1000 },
    "system_cpu_usage": 10000
  },
  "memory_stats": {
    "usage": 209715200,
    "limit": 4294967296,
    "stats": { "cache": 104857600 }
  }
})";

}  // namespace

EBL_TEST(parses_a_realistic_stats_frame) {
  auto stats = parseDockerStats(Json::parse(kSampleFrame));
  EBL_CHECK(stats.has_value());
  EBL_CHECK(stats->cpuPercent > 99.0 && stats->cpuPercent < 101.0);
  // usage 200MB, cache 100MB -> working-set 100MB, limit 4096MB -> ~2.44%
  EBL_CHECK(stats->memUsedMb > 99.0 && stats->memUsedMb < 101.0);
  EBL_CHECK(stats->memLimitMb > 4095.0 && stats->memLimitMb < 4097.0);
  EBL_CHECK(stats->memPercent > 2.0 && stats->memPercent < 3.0);
}

EBL_TEST(falls_back_to_percpu_array_length_when_online_cpus_missing) {
  const char* frame = R"({
    "cpu_stats": {
      "cpu_usage": { "total_usage": 1004, "percpu_usage": [1,2] },
      "system_cpu_usage": 10008
    },
    "precpu_stats": {
      "cpu_usage": { "total_usage": 1000 },
      "system_cpu_usage": 10000
    },
    "memory_stats": { "usage": 1000, "limit": 2000 }
  })";
  // cpuDelta=4, systemDelta=8, onlineCpus falls back to percpu_usage.length=2 -> (4/8)*2*100=100%
  auto stats = parseDockerStats(Json::parse(frame));
  EBL_CHECK(stats.has_value());
  EBL_CHECK(stats->cpuPercent > 99.0 && stats->cpuPercent < 101.0);
}

EBL_TEST(handles_cgroup_v2_inactive_file_instead_of_cache) {
  const char* frame = R"({
    "cpu_stats": { "cpu_usage": { "total_usage": 0 }, "system_cpu_usage": 0, "online_cpus": 1 },
    "precpu_stats": { "cpu_usage": { "total_usage": 0 }, "system_cpu_usage": 0 },
    "memory_stats": {
      "usage": 300000000,
      "limit": 1000000000,
      "stats": { "inactive_file": 100000000 }
    }
  })";
  auto stats = parseDockerStats(Json::parse(frame));
  EBL_CHECK(stats.has_value());
  // working set = 300MB - 100MB = 200MB (raw bytes / 1024^2)
  double expectedMb = 200000000.0 / (1024.0 * 1024.0);
  EBL_CHECK(stats->memUsedMb > expectedMb - 1.0 && stats->memUsedMb < expectedMb + 1.0);
}

EBL_TEST(zero_or_negative_cpu_delta_reports_zero_percent_not_garbage) {
  const char* frame = R"({
    "cpu_stats": { "cpu_usage": { "total_usage": 500 }, "system_cpu_usage": 10000, "online_cpus": 4 },
    "precpu_stats": { "cpu_usage": { "total_usage": 1000 }, "system_cpu_usage": 9000 },
    "memory_stats": { "usage": 0, "limit": 1000 }
  })";
  auto stats = parseDockerStats(Json::parse(frame));
  EBL_CHECK(stats.has_value());
  EBL_CHECK_EQ(stats->cpuPercent, 0.0);
}

EBL_TEST(non_object_input_returns_nullopt) {
  auto stats = parseDockerStats(Json::parse("42"));
  EBL_CHECK(!stats.has_value());
}

EBL_TEST(missing_cpu_stats_returns_nullopt) {
  auto stats = parseDockerStats(Json::parse(R"({"memory_stats":{"usage":1,"limit":2}})"));
  EBL_CHECK(!stats.has_value());
}
