"use client";

import { use, useEffect, useState } from "react";
import { cancelBuild, getBuild, getBuildStats } from "@/lib/api";
import { useBuildSocket } from "@/lib/useBuildSocket";
import StatusPill from "@/components/StatusPill";
import BuildTimeline from "@/components/BuildTimeline";
import LiveLogs from "@/components/LiveLogs";
import ResourceCharts from "@/components/ResourceCharts";
import MetricsPanel from "@/components/MetricsPanel";
import type { BuildPhase, BuildRecord, StatSample } from "@/lib/types";

export default function BuildPage({ params }: { params: Promise<{ id: string }> }) {
  const { id } = use(params);
  const live = useBuildSocket(id);

  // Seed with a REST fetch so the page has data immediately (and works if the WS
  // connection is still negotiating, or the build already finished before this tab
  // opened) — the socket's `snapshot` message then keeps everything current.
  const [seed, setSeed] = useState<{ build: BuildRecord; phases: BuildPhase[] } | null>(null);
  const [seedStats, setSeedStats] = useState<StatSample[]>([]);

  useEffect(() => {
    getBuild(id).then(setSeed).catch(() => {});
    getBuildStats(id).then((r) => setSeedStats(r.samples)).catch(() => {});
  }, [id]);

  const build = live.build ?? seed?.build;
  const phases = live.phases.length > 0 ? live.phases : seed?.phases ?? [];
  const stats = live.stats.length > 0 ? live.stats : seedStats;
  const log = live.log || "";

  if (!build) {
    return <p className="text-sm text-text-dim">Loading build…</p>;
  }

  const cancellable = build.status === "queued" || build.status === "starting" || build.status === "running";

  return (
    <div className="space-y-6">
      <div className="flex flex-wrap items-center justify-between gap-3">
        <div>
          <h1 className="font-display text-xl font-semibold tracking-tight">{build.appName ?? "Build"}</h1>
          <p className="truncate font-mono text-xs text-text-dim">{build.appPath}</p>
        </div>
        <div className="flex items-center gap-3">
          <StatusPill status={build.status} />
          {cancellable && (
            <button
              onClick={() => cancelBuild(id)}
              className="rounded-md border border-danger/40 px-3 py-1.5 font-mono text-xs text-danger transition-colors hover:bg-danger-soft"
            >
              Cancel
            </button>
          )}
        </div>
      </div>

      <BuildTimeline build={build} phases={phases} />

      <div className="grid grid-cols-1 gap-6 xl:grid-cols-5">
        <div className="xl:col-span-3">
          <h2 className="mb-2 font-display text-sm font-semibold">Live log</h2>
          <LiveLogs key={id} text={log} />
        </div>
        <div className="space-y-4 xl:col-span-2">
          <MetricsPanel build={build} />
        </div>
      </div>

      <div>
        <h2 className="mb-3 font-display text-sm font-semibold">Resource usage</h2>
        <ResourceCharts samples={stats} />
      </div>
    </div>
  );
}
