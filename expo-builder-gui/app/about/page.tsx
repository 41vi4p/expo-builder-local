import pkg from "@/package.json";

const REPO_URL = "https://github.com/41vi4p/expo-builder-local";

const details: { label: string; value: React.ReactNode }[] = [
  { label: "Version", value: `v${pkg.version}` },
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
    <div className="space-y-6">
      <div>
        <h1 className="font-display text-2xl font-semibold tracking-tight">About</h1>
        <p className="mt-1 text-sm text-text-dim">expo-builder-local — the GUI, orchestrator, and CLI ship together.</p>
      </div>

      <div className="rounded-lg border border-border bg-surface p-6">
        <div className="flex items-baseline gap-2">
          <span className="font-display text-xl font-semibold tracking-tight">
            expo<span className="text-accent">/</span>builder
          </span>
          <span className="font-mono text-xs text-text-dim">local</span>
        </div>
        <p className="mt-3 max-w-prose text-sm text-text-dim">
          Build managed Expo (SDK 56+) projects into signed Android APK/AABs entirely on your own machine, via a
          disposable Docker container — from the command line (<code className="font-mono text-text">ebl build</code>)
          or this web GUI. Not affiliated with Expo/Google.
        </p>

        <dl className="mt-6 divide-y divide-border border-t border-border">
          {details.map((d) => (
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
