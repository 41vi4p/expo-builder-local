const PHASES = [
  { tag: "setup", title: "Prepare", body: "Your project is copied into a clean, disposable container — nothing on your machine changes." },
  { tag: "install", title: "Install", body: "Dependencies install via npm — cached across builds, so only the first run is slow." },
  { tag: "prebuild", title: "Generate", body: "expo prebuild (or eas build --local) turns your managed project into a real native Android app." },
  { tag: "signing", title: "Sign", body: "Debug-signed by default, or wired up with your own release keystore for the Play Store." },
  { tag: "compile", title: "Compile", body: "Gradle builds the APK or AAB — logs and CPU/memory/network charts stream live." },
  { tag: "collect", title: "Collect", body: "The signed artifact, its SHA-256, and build metrics land in your project's ebl_builds/ folder." },
] as const;

export default function PhasePipeline() {
  return (
    <div className="relative">
      <ol className="grid grid-cols-1 gap-6 sm:grid-cols-2 lg:grid-cols-6 lg:gap-4">
        {PHASES.map((p, i) => (
          <li key={p.tag} className="relative flex flex-col gap-2">
            <div className="flex items-center gap-3 lg:flex-col lg:items-start lg:gap-3">
              <span className="flex h-9 w-9 shrink-0 items-center justify-center rounded-full border border-border bg-surface font-mono text-xs text-text-dim">
                {String(i + 1).padStart(2, "0")}
              </span>
              <span className="phase-tag">{p.tag}</span>
            </div>
            <h3 className="font-display text-base font-semibold">{p.title}</h3>
            <p className="text-sm text-text-dim">{p.body}</p>
          </li>
        ))}
      </ol>
    </div>
  );
}
