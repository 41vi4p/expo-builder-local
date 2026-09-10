package dockerstats

import "testing"

// Ported 1:1 from cli/tests/test_docker_stats.cpp.

// A realistic-shaped Docker /containers/{id}/stats frame, with cpu_stats numbers
// chosen so the expected CPU% comes out to a round number: cpuDelta=2, systemDelta=8,
// onlineCpus=4 -> (2/8)*4*100 = 100%.
const sampleFrame = `{
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
}`

func TestParsesARealisticStatsFrame(t *testing.T) {
	stats, err := Parse([]byte(sampleFrame))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !(stats.CPUPercent > 99.0 && stats.CPUPercent < 101.0) {
		t.Errorf("CPUPercent = %v, want ~100", stats.CPUPercent)
	}
	// usage 200MB, cache 100MB -> working-set 100MB, limit 4096MB -> ~2.44%
	if !(stats.MemUsedMB > 99.0 && stats.MemUsedMB < 101.0) {
		t.Errorf("MemUsedMB = %v, want ~100", stats.MemUsedMB)
	}
	if !(stats.MemLimitMB > 4095.0 && stats.MemLimitMB < 4097.0) {
		t.Errorf("MemLimitMB = %v, want ~4096", stats.MemLimitMB)
	}
	if !(stats.MemPercent > 2.0 && stats.MemPercent < 3.0) {
		t.Errorf("MemPercent = %v, want ~2.44", stats.MemPercent)
	}
}

func TestFallsBackToPercpuArrayLengthWhenOnlineCpusMissing(t *testing.T) {
	frame := `{
    "cpu_stats": {
      "cpu_usage": { "total_usage": 1004, "percpu_usage": [1,2] },
      "system_cpu_usage": 10008
    },
    "precpu_stats": {
      "cpu_usage": { "total_usage": 1000 },
      "system_cpu_usage": 10000
    },
    "memory_stats": { "usage": 1000, "limit": 2000 }
  }`
	// cpuDelta=4, systemDelta=8, onlineCpus falls back to percpu_usage.length=2 -> (4/8)*2*100=100%
	stats, err := Parse([]byte(frame))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if !(stats.CPUPercent > 99.0 && stats.CPUPercent < 101.0) {
		t.Errorf("CPUPercent = %v, want ~100", stats.CPUPercent)
	}
}

func TestHandlesCgroupV2InactiveFileInsteadOfCache(t *testing.T) {
	frame := `{
    "cpu_stats": { "cpu_usage": { "total_usage": 0 }, "system_cpu_usage": 0, "online_cpus": 1 },
    "precpu_stats": { "cpu_usage": { "total_usage": 0 }, "system_cpu_usage": 0 },
    "memory_stats": {
      "usage": 300000000,
      "limit": 1000000000,
      "stats": { "inactive_file": 100000000 }
    }
  }`
	stats, err := Parse([]byte(frame))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	// working set = 300MB - 100MB = 200MB (raw bytes / 1024^2)
	expectedMB := 200000000.0 / (1024.0 * 1024.0)
	if !(stats.MemUsedMB > expectedMB-1.0 && stats.MemUsedMB < expectedMB+1.0) {
		t.Errorf("MemUsedMB = %v, want ~%v", stats.MemUsedMB, expectedMB)
	}
}

func TestZeroOrNegativeCpuDeltaReportsZeroPercentNotGarbage(t *testing.T) {
	frame := `{
    "cpu_stats": { "cpu_usage": { "total_usage": 500 }, "system_cpu_usage": 10000, "online_cpus": 4 },
    "precpu_stats": { "cpu_usage": { "total_usage": 1000 }, "system_cpu_usage": 9000 },
    "memory_stats": { "usage": 0, "limit": 1000 }
  }`
	stats, err := Parse([]byte(frame))
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if stats.CPUPercent != 0.0 {
		t.Errorf("CPUPercent = %v, want 0", stats.CPUPercent)
	}
}

func TestNonObjectInputReturnsError(t *testing.T) {
	if _, err := Parse([]byte("42")); err == nil {
		t.Error("expected an error for non-object input")
	}
}

func TestMissingCpuStatsReturnsError(t *testing.T) {
	if _, err := Parse([]byte(`{"memory_stats":{"usage":1,"limit":2}}`)); err == nil {
		t.Error("expected an error for missing cpu_stats")
	}
}
