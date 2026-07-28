"use client";

import { useEffect, useState } from "react";

const LINES = ["$ ebl build .", "@@PHASE:collect", "✓ app-preview.apk  signed, 41.2 MB"];

export default function TerminalTypewriter() {
  const [reduced, setReduced] = useState(false);
  const [lineIndex, setLineIndex] = useState(0);
  const [charIndex, setCharIndex] = useState(0);

  useEffect(() => {
    const mq = window.matchMedia("(prefers-reduced-motion: reduce)");
    setReduced(mq.matches);
  }, []);

  useEffect(() => {
    if (reduced) return;
    if (lineIndex >= LINES.length) return;
    const current = LINES[lineIndex];
    if (charIndex < current.length) {
      const t = setTimeout(() => setCharIndex((c) => c + 1), 28 + Math.random() * 35);
      return () => clearTimeout(t);
    }
    const t = setTimeout(() => {
      setLineIndex((l) => l + 1);
      setCharIndex(0);
    }, 550);
    return () => clearTimeout(t);
  }, [charIndex, lineIndex, reduced]);

  const doneLines = reduced ? LINES : LINES.slice(0, lineIndex);
  const typingLine = reduced ? null : LINES[lineIndex]?.slice(0, charIndex);
  const finished = reduced || lineIndex >= LINES.length;

  return (
    <div className="rounded-lg border border-border bg-surface/80 px-4 py-3 font-mono text-[13px] leading-relaxed shadow-sm backdrop-blur-sm sm:text-sm">
      {doneLines.map((line, i) => (
        <div key={i} className={i === 0 ? "text-text" : i === 1 ? "text-text-dim" : "text-success"}>
          {line}
        </div>
      ))}
      {typingLine !== null && lineIndex < LINES.length && (
        <div className={lineIndex === 0 ? "text-text" : lineIndex === 1 ? "text-text-dim" : "text-success"}>
          {typingLine}
          <span className="animate-pulse text-accent">▮</span>
        </div>
      )}
      {finished && doneLines.length > 0 && <span className="sr-only">Build complete.</span>}
    </div>
  );
}
