"use client";

import { useEffect, useState } from "react";
import type { BuildPhase, BuildRecord, Engine, SigningMode } from "@/lib/types";

interface PhaseSpec {
  id: string;
  label: string;
}

const GRADLE_PHASES: PhaseSpec[] = [
  { id: "setup", label: "Setup" },
  { id: "install", label: "Install" },
  { id: "prebuild", label: "Prebuild" },
  { id: "signing", label: "Signing" },
  { id: "gradle", label: "Gradle" },
  { id: "collect", label: "Collect" },
  { id: "done", label: "Done" },
];

const EAS_PHASES: PhaseSpec[] = [
  { id: "setup", label: "Setup" },
  { id: "install", label: "Install" },
  { id: "signing", label: "Signing" },
  { id: "eas", label: "EAS build" },
  { id: "collect", label: "Collect" },
  { id: "done", label: "Done" },
];

function phaseSequence(engineResolved: Engine | null, signingMode: SigningMode): PhaseSpec[] {
  const base = engineResolved === "eas" ? EAS_PHASES : GRADLE_PHASES;
  return signingMode === "release" ? base : base.filter((p) => p.id !== "signing");
}

function formatDuration(seconds: number): string {
  if (seconds < 60) return `${Math.round(seconds)}s`;
  const m = Math.floor(seconds / 60);
  const s = Math.round(seconds % 60);
  return `${m}m ${s}s`;
}

/**
 * The dashboard's signature element: a build genuinely *is* a fixed sequence of
 * phases (unlike most numbered-step UI, the ordering here carries real information —
 * what's about to run, what already ran, and how long each step took) so a connected
 * phase rail doubles as both a progress bar and a diagnostic history, rather than a
 * generic percentage bar.
 */
export default function BuildTimeline({ build, phases }: { build: BuildRecord; phases: BuildPhase[] }) {
  const [now, setNow] = useState(() => Date.now());
  const isLive = build.status === "starting" || build.status === "running";

  useEffect(() => {
    if (!isLive) return;
    const id = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(id);
  }, [isLive]);

  const sequence = phaseSequence(build.engineResolved, build.signingMode);
  const byId = new Map(phases.map((p) => [p.phase, p]));
  const currentIndex = sequence.findIndex((p) => p.id === build.currentPhase);

  return (
    <div className="rounded-lg border border-border bg-surface p-5">
      <div className="mb-4 flex flex-wrap items-center justify-between gap-2">
        <div className="font-mono text-sm text-text-dim">
          {build.progress}%{" "}
          {build.status === "running" && build.etaSeconds != null && (
            <span>· ~{formatDuration(build.etaSeconds)} remaining</span>
          )}
          {build.status === "success" && build.durationSeconds != null && (
            <span className="text-success">· finished in {formatDuration(build.durationSeconds)}</span>
          )}
          {build.status === "failed" && <span className="text-danger">· build failed</span>}
        </div>
        {build.engineResolved && (
          <span className="rounded-full bg-surface-2 px-2.5 py-1 font-mono text-xs text-text-dim">
            engine: {build.engineResolved}
          </span>
        )}
      </div>

      <div className="relative flex items-start">
        <div className="absolute left-0 right-0 top-3 h-[2px] bg-border" />
        <div
          className="absolute left-0 top-3 h-[2px] bg-accent transition-[width] duration-500"
          style={{ width: `${Math.min(100, build.progress)}%` }}
        />
        {sequence.map((phase, i) => {
          const record = byId.get(phase.id);
          const isDone = record?.endedAt != null || (currentIndex >= 0 && i < currentIndex) || build.status === "success";
          const isActive = phase.id === build.currentPhase && !isDone;
          const elapsed = record
            ? ((record.endedAt ?? (isActive ? now : record.startedAt)) - record.startedAt) / 1000
            : null;

          return (
            <div key={phase.id} className="relative z-10 flex flex-1 flex-col items-center gap-2 text-center">
              <div
                className={[
                  "flex h-6 w-6 items-center justify-center rounded-full border-2 font-mono text-[10px]",
                  isDone
                    ? "border-accent bg-accent text-[var(--surface)]"
                    : isActive
                      ? "border-accent bg-surface text-accent animate-pulse"
                      : "border-border bg-surface text-text-dim",
                ].join(" ")}
              >
                {isDone ? "✓" : i + 1}
              </div>
              <div className="space-y-0.5">
                <div className={`text-xs font-medium ${isActive ? "text-accent" : isDone ? "text-text" : "text-text-dim"}`}>
                  {phase.label}
                </div>
                {elapsed != null && (
                  <div className="font-mono text-[10px] text-text-dim">{formatDuration(elapsed)}</div>
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
}
