import type { StatSample } from '../types';

/**
 * Normalizes one frame of `docker stats --format json` (as delivered by dockerode's
 * `container.stats({ stream: true })`) into the flat StatSample shape the dashboard
 * charts consume. Uses the same CPU% formula as the `docker` CLI itself.
 */
export function parseDockerStats(raw: any): StatSample | null {
  try {
    const cpuDelta = raw.cpu_stats.cpu_usage.total_usage - raw.precpu_stats.cpu_usage.total_usage;
    const systemDelta = raw.cpu_stats.system_cpu_usage - raw.precpu_stats.system_cpu_usage;
    const numCpus =
      raw.cpu_stats.online_cpus ||
      (Array.isArray(raw.cpu_stats.cpu_usage.percpu_usage) ? raw.cpu_stats.cpu_usage.percpu_usage.length : 1) ||
      1;
    const cpuPct = systemDelta > 0 && cpuDelta > 0 ? (cpuDelta / systemDelta) * numCpus * 100 : 0;

    const memUsageRaw = raw.memory_stats?.usage ?? 0;
    // Subtract page cache so the chart reflects actual working-set memory, not the
    // kernel's disk-cache accounting (matches what `docker stats` shows). Handles
    // both cgroup v1 (`stats.cache`) and cgroup v2 (`stats.inactive_file`) layouts.
    const cache = raw.memory_stats?.stats?.cache ?? raw.memory_stats?.stats?.inactive_file ?? 0;
    const memUsage = Math.max(0, memUsageRaw - cache);
    const memLimit = raw.memory_stats?.limit ?? 1;
    const memMb = memUsage / (1024 * 1024);
    const memPct = memLimit > 0 ? (memUsage / memLimit) * 100 : 0;

    let netRx = 0;
    let netTx = 0;
    if (raw.networks) {
      for (const iface of Object.values<any>(raw.networks)) {
        netRx += iface.rx_bytes ?? 0;
        netTx += iface.tx_bytes ?? 0;
      }
    }

    let blkRead = 0;
    let blkWrite = 0;
    const ioEntries = raw.blkio_stats?.io_service_bytes_recursive ?? [];
    for (const entry of ioEntries) {
      if (entry.op === 'Read' || entry.op === 'read') blkRead += entry.value ?? 0;
      if (entry.op === 'Write' || entry.op === 'write') blkWrite += entry.value ?? 0;
    }

    return {
      ts: Date.now(),
      cpuPct: Number(cpuPct.toFixed(2)),
      memMb: Number(memMb.toFixed(1)),
      memPct: Number(memPct.toFixed(2)),
      netRxBytes: netRx,
      netTxBytes: netTx,
      blkReadBytes: blkRead,
      blkWriteBytes: blkWrite,
    };
  } catch {
    return null; // malformed/partial stats frame — skip it, next tick will recover
  }
}
