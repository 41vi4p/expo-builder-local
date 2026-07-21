"use client";

import { useEffect, useState } from "react";
import Link from "next/link";
import DirectoryBrowser from "@/components/DirectoryBrowser";
import BuildConfigForm from "@/components/BuildConfigForm";
import StatusPill from "@/components/StatusPill";
import { listBuilds } from "@/lib/api";
import type { BuildRecord, ExpoProjectInfo } from "@/lib/types";

function timeAgo(ts: number): string {
  const s = Math.floor((Date.now() - ts) / 1000);
  if (s < 60) return `${s}s ago`;
  if (s < 3600) return `${Math.floor(s / 60)}m ago`;
  if (s < 86400) return `${Math.floor(s / 3600)}h ago`;
  return `${Math.floor(s / 86400)}d ago`;
}

export default function HomePage() {
  const [selected, setSelected] = useState<{ path: string; project: ExpoProjectInfo } | null>(null);
  const [recent, setRecent] = useState<BuildRecord[]>([]);

  useEffect(() => {
    listBuilds(8)
      .then((r) => setRecent(r.builds))
      .catch(() => {});
  }, []);

  return (
    <div className="space-y-8">
      <div>
        <h1 className="font-display text-2xl font-semibold tracking-tight">Build an Android app</h1>
        <p className="mt-1 text-sm text-text-dim">
          Point at any Expo project on this machine and get a signed APK or AAB, built in an isolated container.
        </p>
      </div>

      <div className="grid grid-cols-1 gap-6 lg:grid-cols-2">
        <DirectoryBrowser onSelect={(path, project) => setSelected({ path, project })} />
        {selected ? (
          <BuildConfigForm appPath={selected.path} project={selected.project} />
        ) : (
          <div className="flex items-center justify-center rounded-lg border border-dashed border-border p-8 text-center text-sm text-text-dim">
            Pick an Expo project folder on the left to configure a build.
          </div>
        )}
      </div>

      {recent.length > 0 && (
        <div>
          <div className="mb-3 flex items-center justify-between">
            <h2 className="font-display text-sm font-semibold">Recent builds</h2>
            <Link href="/history" className="font-mono text-xs text-text-dim hover:text-text">
              View all →
            </Link>
          </div>
          <div className="overflow-hidden rounded-lg border border-border">
            {recent.map((b) => (
              <Link
                key={b.id}
                href={`/build/${b.id}`}
                className="flex items-center justify-between gap-3 border-b border-border px-4 py-3 text-sm transition-colors last:border-0 hover:bg-surface-2"
              >
                <div className="min-w-0">
                  <div className="truncate font-medium">{b.appName ?? b.appPath}</div>
                  <div className="font-mono text-xs text-text-dim">
                    {b.profile} · {b.artifactType} · {timeAgo(b.createdAt)}
                  </div>
                </div>
                <StatusPill status={b.status} />
              </Link>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
