const FEATURES = [
  {
    title: "No cloud queue",
    body: "The default Gradle path never touches EAS — no build quota, no account, no waiting in line behind other builds.",
  },
  {
    title: "CLI or GUI, your call",
    body: "ebl build . works standalone from any terminal. Prefer a dashboard? ebl start runs a live web GUI with logs and charts.",
  },
  {
    title: "Disposable containers",
    body: "Every build gets a fresh container with its own Android SDK, Node, and Gradle — torn down after, nothing lingers.",
  },
  {
    title: "Real signing, handled safely",
    body: "Debug by default, or wire in a release keystore — encrypted at rest, decrypted only in memory for the one build that needs it.",
  },
  {
    title: "Multiple EAS accounts",
    body: "Save a token per account; ebl build auto-selects the right one by matching your project's app.json owner.",
  },
  {
    title: "Secrets stay out of your logs",
    body: "Anything that looks like a secret in your .env, eas.json, or google-services.json is redacted from streamed and saved logs.",
  },
] as const;

export default function FeatureGrid() {
  return (
    <div className="grid grid-cols-1 gap-px overflow-hidden rounded-lg border border-border bg-border sm:grid-cols-2 lg:grid-cols-3">
      {FEATURES.map((f) => (
        <div key={f.title} className="bg-surface p-6">
          <h3 className="font-display text-base font-semibold">{f.title}</h3>
          <p className="mt-2 text-sm text-text-dim">{f.body}</p>
        </div>
      ))}
    </div>
  );
}
