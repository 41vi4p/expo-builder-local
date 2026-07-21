import type { BuildStatus } from "@/lib/types";

const STYLES: Record<BuildStatus, { label: string; bg: string; fg: string; dot: string }> = {
  queued: { label: "Queued", bg: "bg-surface-2", fg: "text-text-dim", dot: "bg-text-dim" },
  starting: { label: "Starting", bg: "bg-accent-soft", fg: "text-accent", dot: "bg-accent" },
  running: { label: "Building", bg: "bg-accent-soft", fg: "text-accent", dot: "bg-accent" },
  success: { label: "Success", bg: "bg-success-soft", fg: "text-success", dot: "bg-success" },
  failed: { label: "Failed", bg: "bg-danger-soft", fg: "text-danger", dot: "bg-danger" },
  cancelled: { label: "Cancelled", bg: "bg-surface-2", fg: "text-text-dim", dot: "bg-text-dim" },
};

export default function StatusPill({ status }: { status: BuildStatus }) {
  const s = STYLES[status];
  const pulsing = status === "running" || status === "starting";
  return (
    <span className={`inline-flex items-center gap-1.5 rounded-full px-2.5 py-1 font-mono text-xs font-medium ${s.bg} ${s.fg}`}>
      <span className={`h-1.5 w-1.5 rounded-full ${s.dot} ${pulsing ? "animate-pulse" : ""}`} />
      {s.label}
    </span>
  );
}
