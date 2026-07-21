import type { BuildRecord } from "@/lib/types";
import { artifactUrl } from "@/lib/api";

function formatBytes(bytes: number): string {
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function formatDuration(seconds: number): string {
  const m = Math.floor(seconds / 60);
  const s = seconds % 60;
  return m > 0 ? `${m}m ${s}s` : `${s}s`;
}

function Row({ label, value }: { label: string; value: React.ReactNode }) {
  return (
    <div className="flex items-center justify-between border-b border-border py-2.5 last:border-0">
      <span className="text-sm text-text-dim">{label}</span>
      <span className="font-mono text-sm text-text">{value}</span>
    </div>
  );
}

export default function MetricsPanel({ build }: { build: BuildRecord }) {
  if (build.status === "failed") {
    return (
      <div className="rounded-lg border border-danger/30 bg-danger-soft p-5">
        <h3 className="font-display text-sm font-semibold text-danger">Build failed</h3>
        <p className="mt-2 whitespace-pre-wrap font-mono text-xs text-text">
          {build.error ?? "No error detail was captured — check the log above."}
        </p>
      </div>
    );
  }

  if (build.status !== "success") return null;

  const delta =
    build.previousArtifactSizeBytes != null && build.artifactSizeBytes != null
      ? build.artifactSizeBytes - build.previousArtifactSizeBytes
      : null;

  return (
    <div className="rounded-lg border border-border bg-surface p-5">
      <div className="mb-2 flex items-center justify-between">
        <h3 className="font-display text-sm font-semibold">Build metrics</h3>
        <a
          href={artifactUrl(build.id)}
          download
          className="rounded-md bg-accent px-3 py-1.5 font-mono text-xs font-medium text-[#1a1006] transition-opacity hover:opacity-90"
        >
          Download {build.artifactType.toUpperCase()}
        </a>
      </div>
      <div>
        <Row
          label="Artifact size"
          value={
            <span>
              {build.artifactSizeBytes != null ? formatBytes(build.artifactSizeBytes) : "—"}
              {delta != null && (
                <span className={delta > 0 ? "text-danger" : delta < 0 ? "text-success" : "text-text-dim"}>
                  {" "}
                  ({delta > 0 ? "+" : ""}
                  {formatBytes(Math.abs(delta))} {delta > 0 ? "larger" : delta < 0 ? "smaller" : "unchanged"})
                </span>
              )}
            </span>
          }
        />
        <Row label="Build time" value={build.durationSeconds != null ? formatDuration(build.durationSeconds) : "—"} />
        <Row label="Version" value={`${build.versionName ?? "?"}${build.versionCode ? ` (${build.versionCode})` : ""}`} />
        <Row label="Application ID" value={build.applicationId ?? "—"} />
        <Row label="Profile / engine" value={`${build.profile} · ${build.engineResolved ?? "?"}`} />
        <Row label="Signing" value={build.signingMode === "release" ? "Release (custom keystore)" : "Debug"} />
        {build.gitCommit && <Row label="Git" value={`${build.gitBranch ?? ""}@${build.gitCommit}`} />}
        <Row label="SHA-256" value={<span className="break-all text-xs">{build.artifactSha256 ?? "—"}</span>} />
      </div>
    </div>
  );
}
