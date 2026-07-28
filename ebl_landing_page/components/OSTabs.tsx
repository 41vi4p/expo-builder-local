"use client";

import { useEffect, useState } from "react";

export default function OSTabs({ linux, windows }: { linux: React.ReactNode; windows: React.ReactNode }) {
  const [os, setOs] = useState<"linux" | "windows">("linux");

  // Best-effort default to the visitor's actual OS; either tab is always one click away.
  useEffect(() => {
    if (navigator.userAgent.toLowerCase().includes("windows")) setOs("windows");
  }, []);

  const tabClass = (active: boolean) =>
    `rounded-md px-4 py-1.5 text-sm font-medium transition-colors ${
      active ? "bg-surface text-text shadow-sm" : "text-text-dim hover:text-text"
    }`;

  return (
    <div>
      <div className="inline-flex rounded-lg border border-border bg-surface-2 p-1" role="tablist" aria-label="Operating system">
        <button type="button" role="tab" aria-selected={os === "linux"} onClick={() => setOs("linux")} className={tabClass(os === "linux")}>
          Linux
        </button>
        <button type="button" role="tab" aria-selected={os === "windows"} onClick={() => setOs("windows")} className={tabClass(os === "windows")}>
          Windows
        </button>
      </div>
      <div className="mt-8">{os === "linux" ? linux : windows}</div>
    </div>
  );
}
