"use client";

import { useEffect, useRef } from "react";
import "@xterm/xterm/css/xterm.css";

/**
 * Renders the build's raw stdout/stderr as a real terminal rather than a plain
 * scrolling <pre>. This matters specifically because Gradle's `--console=rich` output
 * uses carriage returns to overwrite its own progress line in place — a plain text
 * view would show every intermediate percentage stacked on its own line; a terminal
 * emulator renders it the way a developer would see it running gradlew locally.
 *
 * `@xterm/xterm` touches `window`/`document`, so it's imported dynamically inside the
 * effect (client-only) rather than statically at module scope, keeping this component
 * safe under Next's server-rendered-then-hydrated client component pass.
 */
export default function LiveLogs({ text }: { text: string }) {
  const containerRef = useRef<HTMLDivElement>(null);
  const termRef = useRef<{ write: (s: string) => void } | null>(null);
  const writtenLengthRef = useRef(0);

  useEffect(() => {
    let disposed = false;
    let dispose: (() => void) | null = null;

    (async () => {
      const [{ Terminal }, { FitAddon }] = await Promise.all([
        import("@xterm/xterm"),
        import("@xterm/addon-fit"),
      ]);
      if (disposed || !containerRef.current) return;

      const term = new Terminal({
        convertEol: true,
        fontFamily: "var(--font-data), ui-monospace, monospace",
        fontSize: 13,
        theme: {
          background: "#00000000",
          foreground: "#c8cfdb",
          cursor: "#e8944a",
        },
        disableStdin: true,
        scrollback: 20000,
      });
      const fit = new FitAddon();
      term.loadAddon(fit);
      term.open(containerRef.current);
      fit.fit();
      termRef.current = term;

      // Write anything that had already arrived before xterm finished loading.
      if (writtenLengthRef.current < text.length) {
        term.write(text.slice(writtenLengthRef.current));
        writtenLengthRef.current = text.length;
      }

      const onResize = () => fit.fit();
      window.addEventListener("resize", onResize);
      dispose = () => {
        window.removeEventListener("resize", onResize);
        term.dispose();
      };
    })();

    return () => {
      disposed = true;
      dispose?.();
      termRef.current = null;
    };
    // Terminal is constructed once per mount; new text is streamed in via the effect below.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  useEffect(() => {
    if (!termRef.current) return;
    if (text.length > writtenLengthRef.current) {
      termRef.current.write(text.slice(writtenLengthRef.current));
      writtenLengthRef.current = text.length;
    } else if (text.length < writtenLengthRef.current) {
      // Log was reset (e.g. viewing a different build) — nothing incremental to write;
      // the container remounts via `key` in the parent so this path is defensive only.
      writtenLengthRef.current = text.length;
    }
  }, [text]);

  return (
    <div className="rounded-lg border border-border bg-[#0a0e14] p-2">
      <div ref={containerRef} className="h-[420px] w-full" />
    </div>
  );
}
