"use client";

import { useEffect, useMemo, useState } from "react";
import Link from "next/link";
import { Area, ComposedChart, ResponsiveContainer, Tooltip, XAxis, YAxis } from "recharts";
import { listBuilds } from "@/lib/api";
import StatusPill from "@/components/StatusPill";
import type { BuildRecord } from "@/lib/types";

function formatBytes(bytes: number): string {
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function formatDate(ts: number): string {
  return new Date(ts).toLocaleString(undefined, { month: "short", day: "numeric", hour: "2-digit", minute: "2-digit" });
}

export default function HistoryPage() {
  const [builds, setBuilds] = useState<BuildRecord[]>([]);

  useEffect(() => {
    listBuilds(200).then((r) => setBuilds(r.builds));
  }, []);

  const sizeTrend = useMemo(
    () =>
      builds
        .filter((b) => b.status === "success" && b.artifactSizeBytes != null)
        .slice()
        .reverse()
        .map((b) => ({ ts: b.createdAt, size: b.artifactSizeBytes as number, label: b.appName ?? "" })),
    [builds]
  );

  return (
    <div className="space-y-6">
      <div>
        <h1 className="font-display text-2xl font-semibold tracking-tight">Build history</h1>
        <p className="mt-1 text-sm text-text-dim">Every build run on this machine, most recent first.</p>
      </div>

      {sizeTrend.length > 1 && (
        <div className="rounded-lg border border-border bg-surface p-4">
          <h3 className="mb-2 font-display text-sm font-semibold">Artifact size over time</h3>
          <div className="h-[140px] w-full">
            <ResponsiveContainer>
              <ComposedChart data={sizeTrend} margin={{ top: 4, right: 4, left: -16, bottom: 0 }}>
                <XAxis dataKey="ts" tickFormatter={formatDate} tick={{ fill: "var(--text-dim)", fontSize: 11 }} axisLine={{ stroke: "var(--border)" }} tickLine={false} minTickGap={60} />
                <YAxis tickFormatter={formatBytes} tick={{ fill: "var(--text-dim)", fontSize: 11 }} axisLine={false} tickLine={false} width={56} />
                <Tooltip
                  formatter={(v: number) => [formatBytes(v), "Size"]}
                  labelFormatter={(v: number) => formatDate(v)}
                  contentStyle={{ background: "var(--surface-2)", border: "1px solid var(--border)", fontSize: 12 }}
                />
                <Area type="monotone" dataKey="size" stroke="#2a78d6" strokeWidth={2} fill="#2a78d6" fillOpacity={0.1} isAnimationActive={false} />
              </ComposedChart>
            </ResponsiveContainer>
          </div>
        </div>
      )}

      <div className="overflow-hidden rounded-lg border border-border">
        <table className="w-full text-sm">
          <thead>
            <tr className="border-b border-border bg-surface-2 text-left font-mono text-xs text-text-dim">
              <th className="px-4 py-2 font-medium">App</th>
              <th className="px-4 py-2 font-medium">Profile</th>
              <th className="px-4 py-2 font-medium">Status</th>
              <th className="px-4 py-2 font-medium">Duration</th>
              <th className="px-4 py-2 font-medium">Size</th>
              <th className="px-4 py-2 font-medium">Started</th>
            </tr>
          </thead>
          <tbody>
            {builds.map((b) => (
              <tr key={b.id} className="border-b border-border last:border-0 hover:bg-surface-2">
                <td className="px-4 py-2.5">
                  <Link href={`/build/${b.id}`} className="font-medium hover:underline">
                    {b.appName ?? b.appPath}
                  </Link>
                </td>
                <td className="px-4 py-2.5 font-mono text-xs text-text-dim">
                  {b.profile} · {b.artifactType}
                </td>
                <td className="px-4 py-2.5">
                  <StatusPill status={b.status} />
                </td>
                <td className="px-4 py-2.5 font-mono text-xs text-text-dim">
                  {b.durationSeconds != null ? `${b.durationSeconds}s` : "—"}
                </td>
                <td className="px-4 py-2.5 font-mono text-xs text-text-dim">
                  {b.artifactSizeBytes != null ? formatBytes(b.artifactSizeBytes) : "—"}
                </td>
                <td className="px-4 py-2.5 font-mono text-xs text-text-dim">{formatDate(b.createdAt)}</td>
              </tr>
            ))}
            {builds.length === 0 && (
              <tr>
                <td colSpan={6} className="px-4 py-8 text-center text-text-dim">
                  No builds yet.
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>
    </div>
  );
}
