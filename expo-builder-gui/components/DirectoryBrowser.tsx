"use client";

import { useEffect, useState } from "react";
import { listDir, listRoots } from "@/lib/api";
import type { DirEntry, ExpoProjectInfo } from "@/lib/types";

export default function DirectoryBrowser({
  onSelect,
}: {
  onSelect: (path: string, project: ExpoProjectInfo) => void;
}) {
  const [path, setPath] = useState<string | null>(null);
  const [parent, setParent] = useState<string | null>(null);
  const [entries, setEntries] = useState<DirEntry[]>([]);
  const [project, setProject] = useState<ExpoProjectInfo | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    listRoots()
      .then((r) => navigate(r.roots[0]))
      .catch((err) => setError(err.message));
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  function navigate(target: string) {
    setLoading(true);
    setError(null);
    listDir(target)
      .then((res) => {
        setPath(res.path);
        setParent(res.parent);
        setEntries(res.entries);
        setProject(res.project);
      })
      .catch((err) => setError(err.message))
      .finally(() => setLoading(false));
  }

  return (
    <div className="rounded-lg border border-border bg-surface p-4">
      <div className="mb-3 flex items-center justify-between gap-3">
        <h3 className="font-display text-sm font-semibold">Project folder</h3>
        {path && (
          <button
            onClick={() => navigate(path)}
            className="font-mono text-xs text-text-dim transition-colors hover:text-text"
            title="Refresh"
          >
            ↻ refresh
          </button>
        )}
      </div>

      <div className="mb-3 truncate rounded-md bg-surface-2 px-3 py-2 font-mono text-xs text-text-dim">
        {path ?? "Loading…"}
      </div>

      {error && <p className="mb-3 font-mono text-xs text-danger">{error}</p>}

      <div className="console-scroll max-h-64 overflow-y-auto rounded-md border border-border">
        {parent && (
          <button
            onClick={() => navigate(parent)}
            className="flex w-full items-center gap-2 border-b border-border px-3 py-2 text-left text-sm text-text-dim transition-colors hover:bg-surface-2"
          >
            <span className="font-mono">..</span>
          </button>
        )}
        {loading ? (
          <div className="px-3 py-4 text-center text-sm text-text-dim">Loading…</div>
        ) : entries.length === 0 ? (
          <div className="px-3 py-4 text-center text-sm text-text-dim">No subfolders here</div>
        ) : (
          entries.map((entry) => (
            <button
              key={entry.path}
              onClick={() => navigate(entry.path)}
              className="flex w-full items-center gap-2 border-b border-border px-3 py-2 text-left text-sm transition-colors last:border-0 hover:bg-surface-2"
            >
              <span className="text-text-dim">📁</span>
              <span className="truncate">{entry.name}</span>
            </button>
          ))
        )}
      </div>

      {project && (
        <div className="mt-4 rounded-md border border-border p-3">
          {project.isExpoProject ? (
            <div className="space-y-1">
              <div className="flex items-center gap-2">
                <span className="h-1.5 w-1.5 rounded-full bg-success" />
                <span className="font-mono text-xs text-success">Expo project detected</span>
              </div>
              <div className="font-mono text-xs text-text-dim">
                {project.name} · v{project.version}
                {project.androidPackage && <> · {project.androidPackage}</>}
              </div>
              {!project.hasEnvFile && (
                <div className="font-mono text-xs text-accent">No .env found — build may be missing config</div>
              )}
              <button
                onClick={() => path && onSelect(path, project)}
                className="mt-2 w-full rounded-md bg-accent px-3 py-2 font-mono text-xs font-medium text-[#1a1006] transition-opacity hover:opacity-90"
              >
                Use this folder
              </button>
            </div>
          ) : (
            <div className="flex items-center gap-2">
              <span className="h-1.5 w-1.5 rounded-full bg-text-dim" />
              <span className="font-mono text-xs text-text-dim">{project.reason ?? "Not an Expo project"}</span>
            </div>
          )}
        </div>
      )}
    </div>
  );
}
