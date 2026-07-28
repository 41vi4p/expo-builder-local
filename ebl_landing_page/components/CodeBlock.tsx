"use client";

import { useState } from "react";

export default function CodeBlock({ code, label }: { code: string; label?: string }) {
  const [copied, setCopied] = useState(false);

  async function copy() {
    try {
      await navigator.clipboard.writeText(code);
      setCopied(true);
      setTimeout(() => setCopied(false), 1800);
    } catch {
      // Clipboard API unavailable (e.g. insecure context) — nothing to fall back to
      // gracefully; the code is still selectable/visible.
    }
  }

  return (
    <div className="overflow-hidden rounded-lg border border-border bg-surface-2">
      {label && (
        <div className="flex items-center justify-between border-b border-border px-4 py-2">
          <span className="font-mono text-xs text-text-dim">{label}</span>
        </div>
      )}
      <div className="relative">
        <pre className="console-scroll overflow-x-auto px-4 py-3.5 font-mono text-[13px] leading-relaxed sm:text-sm">
          <code>{code}</code>
        </pre>
        <button
          type="button"
          onClick={copy}
          aria-label="Copy to clipboard"
          className="absolute right-2.5 top-2.5 rounded-md border border-border bg-surface px-2 py-1 font-mono text-xs text-text-dim transition-colors hover:border-accent hover:text-accent"
        >
          {copied ? "Copied" : "Copy"}
        </button>
      </div>
    </div>
  );
}
