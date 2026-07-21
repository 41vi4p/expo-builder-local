"use client";

import { useMemo } from "react";
import {
  Area,
  CartesianGrid,
  ComposedChart,
  Legend,
  Line,
  ResponsiveContainer,
  Tooltip,
  XAxis,
  YAxis,
} from "recharts";
import type { StatSample } from "@/lib/types";
import { useIsDarkMode } from "@/lib/useIsDarkMode";

// Validated categorical pair (blue/orange, first two slots of the reference palette —
// see dataviz skill, references/palette.md). Passed through validate_palette.js for
// both light and dark against this app's chart surfaces; do not reorder or swap hues
// without re-validating. Color follows the *entity* (primary vs secondary resource)
// consistently across all three charts below.
const SERIES_1_LIGHT = "#2a78d6";
const SERIES_1_DARK = "#3987e5";
const SERIES_2_LIGHT = "#eb6834";
const SERIES_2_DARK = "#d95926";

function formatBytesRate(bytesPerSec: number): string {
  if (bytesPerSec < 1024) return `${bytesPerSec.toFixed(0)} B/s`;
  if (bytesPerSec < 1024 * 1024) return `${(bytesPerSec / 1024).toFixed(1)} KB/s`;
  return `${(bytesPerSec / (1024 * 1024)).toFixed(1)} MB/s`;
}

function formatTime(ts: number): string {
  const d = new Date(ts);
  return d.toLocaleTimeString(undefined, { minute: "2-digit", second: "2-digit" });
}

interface ChartRow {
  ts: number;
  cpuPct: number;
  memPct: number;
  memMb: number;
  netRxRate: number;
  netTxRate: number;
  blkReadRate: number;
  blkWriteRate: number;
}

function toRows(samples: StatSample[]): ChartRow[] {
  const rows: ChartRow[] = [];
  for (let i = 0; i < samples.length; i++) {
    const cur = samples[i];
    const prev = samples[i - 1];
    const dtSec = prev ? Math.max(0.25, (cur.ts - prev.ts) / 1000) : 1;
    rows.push({
      ts: cur.ts,
      cpuPct: cur.cpuPct,
      memPct: cur.memPct,
      memMb: cur.memMb,
      netRxRate: prev ? Math.max(0, (cur.netRxBytes - prev.netRxBytes) / dtSec) : 0,
      netTxRate: prev ? Math.max(0, (cur.netTxBytes - prev.netTxBytes) / dtSec) : 0,
      blkReadRate: prev ? Math.max(0, (cur.blkReadBytes - prev.blkReadBytes) / dtSec) : 0,
      blkWriteRate: prev ? Math.max(0, (cur.blkWriteBytes - prev.blkWriteBytes) / dtSec) : 0,
    });
  }
  return rows;
}

function ChartCard({
  title,
  subtitle,
  children,
}: {
  title: string;
  subtitle: string;
  children: React.ReactNode;
}) {
  return (
    <div className="rounded-lg border border-border bg-surface p-4">
      <div className="mb-1 flex items-baseline justify-between">
        <h3 className="font-display text-sm font-semibold">{title}</h3>
        <span className="font-mono text-xs text-text-dim">{subtitle}</span>
      </div>
      <div className="h-[180px] w-full">{children}</div>
    </div>
  );
}

function ChartTooltip({
  active,
  payload,
  label,
  formatValue,
}: {
  active?: boolean;
  payload?: Array<{ name: string; value: number; color: string }>;
  label?: number;
  formatValue: (v: number) => string;
}) {
  if (!active || !payload || payload.length === 0) return null;
  return (
    <div className="rounded-md border border-border bg-surface-2 px-3 py-2 text-xs shadow-lg">
      <div className="mb-1 font-mono text-text-dim">{label ? formatTime(label) : ""}</div>
      {payload.map((entry) => (
        <div key={entry.name} className="flex items-center gap-2">
          <span className="inline-block h-[2px] w-3" style={{ background: entry.color }} />
          <span className="font-mono font-semibold text-text">{formatValue(entry.value)}</span>
          <span className="text-text-dim">{entry.name}</span>
        </div>
      ))}
    </div>
  );
}

const gridStroke = "var(--border)";
const tickStyle = { fill: "var(--text-dim)", fontSize: 11, fontFamily: "var(--font-data)" };

/** Three single-axis charts (never dual-axis): CPU vs Memory (both %), network
 * throughput (rx/tx, both bytes/sec), disk throughput (read/write, both bytes/sec).
 * Each pair shares the same validated categorical color across all three charts so
 * "primary resource" and "secondary resource" read consistently at a glance. */
export default function ResourceCharts({ samples }: { samples: StatSample[] }) {
  const rows = useMemo(() => toRows(samples), [samples]);
  const isDark = useIsDarkMode();
  const series1 = isDark ? SERIES_1_DARK : SERIES_1_LIGHT;
  const series2 = isDark ? SERIES_2_DARK : SERIES_2_LIGHT;

  if (rows.length === 0) {
    return (
      <div className="rounded-lg border border-dashed border-border p-8 text-center text-sm text-text-dim">
        Resource usage will appear here once the build container starts.
      </div>
    );
  }

  return (
    <div className="grid grid-cols-1 gap-4 sm:grid-cols-2 xl:grid-cols-3">
      <ChartCard
        title="CPU & memory"
        subtitle={`${rows[rows.length - 1].cpuPct.toFixed(0)}% cpu · ${rows[rows.length - 1].memMb.toFixed(0)} MB`}
      >
        <ResponsiveContainer>
          <ComposedChart data={rows} margin={{ top: 4, right: 4, left: -16, bottom: 0 }}>
            <CartesianGrid stroke={gridStroke} vertical={false} strokeDasharray="0" />
            <XAxis dataKey="ts" tickFormatter={formatTime} tick={tickStyle} axisLine={{ stroke: gridStroke }} tickLine={false} minTickGap={40} />
            <YAxis domain={[0, 100]} tick={tickStyle} axisLine={false} tickLine={false} width={36} />
            <Tooltip content={<ChartTooltip formatValue={(v) => `${v.toFixed(1)}%`} />} />
            <Legend wrapperStyle={{ fontSize: 11, fontFamily: "var(--font-data)", color: "var(--text-dim)" }} />
            <Area type="monotone" dataKey="cpuPct" name="CPU %" stroke="none" fill={series1} fillOpacity={0.1} isAnimationActive={false} />
            <Line type="monotone" dataKey="cpuPct" name="CPU %" stroke={series1} strokeWidth={2} dot={false} isAnimationActive={false} />
            <Area type="monotone" dataKey="memPct" name="Memory %" stroke="none" fill={series2} fillOpacity={0.1} isAnimationActive={false} />
            <Line type="monotone" dataKey="memPct" name="Memory %" stroke={series2} strokeWidth={2} dot={false} isAnimationActive={false} />
          </ComposedChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard
        title="Network I/O"
        subtitle={`${formatBytesRate(rows[rows.length - 1].netRxRate)} in`}
      >
        <ResponsiveContainer>
          <ComposedChart data={rows} margin={{ top: 4, right: 4, left: -16, bottom: 0 }}>
            <CartesianGrid stroke={gridStroke} vertical={false} strokeDasharray="0" />
            <XAxis dataKey="ts" tickFormatter={formatTime} tick={tickStyle} axisLine={{ stroke: gridStroke }} tickLine={false} minTickGap={40} />
            <YAxis tickFormatter={formatBytesRate} tick={tickStyle} axisLine={false} tickLine={false} width={56} />
            <Tooltip content={<ChartTooltip formatValue={formatBytesRate} />} />
            <Legend wrapperStyle={{ fontSize: 11, fontFamily: "var(--font-data)", color: "var(--text-dim)" }} />
            <Area type="monotone" dataKey="netRxRate" name="Received" stroke="none" fill={series1} fillOpacity={0.1} isAnimationActive={false} />
            <Line type="monotone" dataKey="netRxRate" name="Received" stroke={series1} strokeWidth={2} dot={false} isAnimationActive={false} />
            <Area type="monotone" dataKey="netTxRate" name="Sent" stroke="none" fill={series2} fillOpacity={0.1} isAnimationActive={false} />
            <Line type="monotone" dataKey="netTxRate" name="Sent" stroke={series2} strokeWidth={2} dot={false} isAnimationActive={false} />
          </ComposedChart>
        </ResponsiveContainer>
      </ChartCard>

      <ChartCard
        title="Disk I/O"
        subtitle={`${formatBytesRate(rows[rows.length - 1].blkWriteRate)} write`}
      >
        <ResponsiveContainer>
          <ComposedChart data={rows} margin={{ top: 4, right: 4, left: -16, bottom: 0 }}>
            <CartesianGrid stroke={gridStroke} vertical={false} strokeDasharray="0" />
            <XAxis dataKey="ts" tickFormatter={formatTime} tick={tickStyle} axisLine={{ stroke: gridStroke }} tickLine={false} minTickGap={40} />
            <YAxis tickFormatter={formatBytesRate} tick={tickStyle} axisLine={false} tickLine={false} width={56} />
            <Tooltip content={<ChartTooltip formatValue={formatBytesRate} />} />
            <Legend wrapperStyle={{ fontSize: 11, fontFamily: "var(--font-data)", color: "var(--text-dim)" }} />
            <Area type="monotone" dataKey="blkReadRate" name="Read" stroke="none" fill={series1} fillOpacity={0.1} isAnimationActive={false} />
            <Line type="monotone" dataKey="blkReadRate" name="Read" stroke={series1} strokeWidth={2} dot={false} isAnimationActive={false} />
            <Area type="monotone" dataKey="blkWriteRate" name="Write" stroke="none" fill={series2} fillOpacity={0.1} isAnimationActive={false} />
            <Line type="monotone" dataKey="blkWriteRate" name="Write" stroke={series2} strokeWidth={2} dot={false} isAnimationActive={false} />
          </ComposedChart>
        </ResponsiveContainer>
      </ChartCard>
    </div>
  );
}
