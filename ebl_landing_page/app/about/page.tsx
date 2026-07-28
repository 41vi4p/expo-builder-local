import type { Metadata } from "next";

export const metadata: Metadata = {
  title: "About — expo-builder-local",
  description: "Why expo-builder-local exists, how it's built, and who maintains it.",
};

const REPO_URL = "https://github.com/41vi4p/expo-builder-local";

const STACK = [
  { label: "CLI", value: "C++17, CMake, libcurl, OpenSSL" },
  { label: "Orchestrator", value: "Fastify, dockerode, better-sqlite3, ws" },
  { label: "GUI", value: "Next.js 16, Tailwind v4" },
  { label: "Runner image", value: "Node 22 LTS, JDK 17, Android SDK, eas-cli" },
];

const DETAILS = [
  {
    label: "Version",
    value: (
      <a href={`${REPO_URL}/releases`} target="_blank" rel="noopener noreferrer" className="text-accent hover:underline">
        Latest release ↗
      </a>
    ),
  },
  { label: "Developer", value: "41vi4p" },
  { label: "License", value: "GNU General Public License v3.0 (GPL-3.0)" },
  {
    label: "Repository",
    value: (
      <a href={REPO_URL} target="_blank" rel="noopener noreferrer" className="text-accent hover:underline">
        {REPO_URL}
      </a>
    ),
  },
];

export default function AboutPage() {
  return (
    <div className="mx-auto max-w-3xl px-6 py-12 sm:py-16">
      <span className="phase-tag">about</span>
      <h1 className="mt-3 font-display text-3xl font-semibold tracking-tight sm:text-4xl">About</h1>

      <div className="mt-8 space-y-4 text-text-dim">
        <p>
          Expo&apos;s managed workflow normally means either <code className="font-mono text-accent">eas build</code>{" "}
          — cloud, costs money or quota, needs an Expo account — or manually running{" "}
          <code className="font-mono text-accent">expo prebuild</code> plus Gradle yourself, every time.
          expo-builder-local wraps the second path in a disposable container, driven by a CLI, a GUI, or both, so any
          developer can produce a signed build without setting up an Android SDK locally or learning Gradle.
        </p>
        <p>
          It&apos;s three pieces that ship together — a standalone C++ CLI (<code className="font-mono text-accent">ebl</code>),
          a Fastify orchestrator, and a Next.js web GUI — plus a disposable Android toolchain image spun up fresh for
          every single build and torn down after. Not affiliated with Expo or Google.
        </p>
      </div>

      <div className="mt-10 rounded-lg border border-border bg-surface-2 p-6">
        <h2 className="font-display text-base font-semibold">Stack</h2>
        <dl className="mt-4 grid grid-cols-1 gap-x-6 gap-y-3 sm:grid-cols-2">
          {STACK.map((s) => (
            <div key={s.label} className="flex flex-col gap-0.5">
              <dt className="font-mono text-xs text-text-dim">{s.label}</dt>
              <dd className="text-sm">{s.value}</dd>
            </div>
          ))}
        </dl>
      </div>

      <div className="mt-6 rounded-lg border border-border bg-surface p-6">
        <dl className="divide-y divide-border">
          {DETAILS.map((d) => (
            <div key={d.label} className="grid grid-cols-3 gap-4 py-3 text-sm">
              <dt className="text-text-dim">{d.label}</dt>
              <dd className="col-span-2 font-mono text-xs sm:text-sm">{d.value}</dd>
            </div>
          ))}
        </dl>
      </div>
    </div>
  );
}
