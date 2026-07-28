import type { Metadata } from "next";
import CodeBlock from "@/components/CodeBlock";

export const metadata: Metadata = {
  title: "Docs — expo-builder-local",
  description: "Quick start, commands, build engines, signing, security, and troubleshooting for expo-builder-local.",
};

const NAV = [
  { href: "#quick-start", label: "Quick start" },
  { href: "#commands", label: "Commands" },
  { href: "#build-engines", label: "Build engines" },
  { href: "#accounts", label: "Multiple accounts" },
  { href: "#signing", label: "Signing" },
  { href: "#security", label: "Security" },
  { href: "#troubleshooting", label: "Troubleshooting" },
];

const COMMANDS = [
  { cmd: "ebl setup", body: "One-time: checks Docker is installed and running (offers to install it if not), then pulls the runner/orchestrator/web images." },
  { cmd: "ebl config", body: "Interactive wizard: projects folder, a default Expo token plus optional per-account tokens, orchestrator/web ports." },
  { cmd: "ebl start", body: "Runs the orchestrator + web GUI as Docker containers, waits for both to report healthy, prints the GUI URL." },
  { cmd: "ebl stop", body: "Stops and removes those two containers. Build history/keystores live in a separate volume and are preserved." },
  { cmd: "ebl build [path]", body: "Builds an Expo project. Works completely standalone — no setup/config/start required." },
];

const ENGINES = [
  { name: "Gradle (local)", how: "expo prebuild generates the native android/ project, then Gradle compiles it directly in the container.", account: "No — fully offline once dependencies are cached." },
  { name: "EAS (local)", how: "eas build --local — the same command EAS's own cloud workers run, just on your machine.", account: "Yes — needs an Expo access token." },
  { name: "Auto (default)", how: "Uses EAS if the project has an eas.json and a token is available, otherwise falls back to Gradle.", account: "Optional." },
];

const TROUBLESHOOTING = [
  { q: "ebl setup says Docker isn't reachable after installing it", a: "Log out and back in (or run newgrp docker) so your user session picks up docker-group membership, then re-run ebl setup." },
  { q: "\"Path is outside the configured allowed roots\" (GUI)", a: "The projects folder set via ebl config doesn't cover the folder you picked." },
  { q: "Build hangs at \"Install\"", a: "First build for a project downloads its full node_modules; later builds reuse the shared caches and are much faster." },
  { q: "\"eas build --local failed\" / credential errors", a: "The EAS engine needs a real Expo access token (ebl config, EXPO_TOKEN, or --expo-token), and a valid eas.json profile." },
  { q: "AAB isn't accepted by the Play Store", a: "Make sure you built with Release signing and a real upload keystore, not the debug default." },
];

function Section({ id, title, children }: { id: string; title: string; children: React.ReactNode }) {
  return (
    <section id={id} className="scroll-mt-24 border-b border-border py-12 first:pt-0">
      <h2 className="font-display text-2xl font-semibold tracking-tight">{title}</h2>
      <div className="mt-5 space-y-4 text-sm text-text-dim sm:text-base [&_code]:font-mono [&_code]:text-accent [&_strong]:text-text">
        {children}
      </div>
    </section>
  );
}

export default function DocsPage() {
  return (
    <div className="mx-auto max-w-6xl px-6 py-12 sm:py-16">
      <span className="phase-tag">docs</span>
      <h1 className="mt-3 font-display text-3xl font-semibold tracking-tight sm:text-4xl">Documentation</h1>
      <p className="mt-2 max-w-2xl text-text-dim">
        Everything to go from a fresh machine to a signed APK. For the exhaustive reference, see the{" "}
        <a
          href="https://github.com/41vi4p/expo-builder-local#readme"
          target="_blank"
          rel="noopener noreferrer"
          className="text-accent hover:underline"
        >
          full README
        </a>
        .
      </p>

      <div className="mt-10 grid grid-cols-1 gap-10 lg:grid-cols-[200px_1fr]">
        <nav className="hidden lg:block">
          <ul className="sticky top-24 space-y-1 font-mono text-sm">
            {NAV.map((n) => (
              <li key={n.href}>
                <a href={n.href} className="block rounded-md px-2 py-1.5 text-text-dim transition-colors hover:bg-surface-2 hover:text-text">
                  {n.label}
                </a>
              </li>
            ))}
          </ul>
        </nav>

        <div className="min-w-0">
          <Section id="quick-start" title="Quick start">
            <p>Install via the hosted APT repo (recommended — picks up new releases automatically):</p>
            <CodeBlock
              label="bash"
              code={`curl -fsSL https://41vi4p.github.io/expo-builder-local/apt/pubkey.gpg | sudo gpg --dearmor -o /usr/share/keyrings/ebl-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/ebl-archive-keyring.gpg] https://41vi4p.github.io/expo-builder-local/apt stable main" | sudo tee /etc/apt/sources.list.d/ebl.list
sudo apt update && sudo apt install ebl`}
            />
            <p>Or the one-line installer — see the <a href="/download" className="text-accent hover:underline">Download page</a> for every option.</p>
            <p>Then:</p>
            <CodeBlock
              label="bash"
              code={`ebl setup     # checks/installs Docker, pulls the runner/orchestrator/web images
ebl config    # interactive: your projects folder, Expo token, ports
ebl start     # runs the orchestrator + web GUI as containers, prints the GUI link

cd /path/to/your/expo/app
ebl build .              # signed APK, auto engine
ebl build . --prod       # shortcut for --artifact aab --profile production`}
            />
            <p>
              <code>ebl build</code> never needs <code>setup</code>/<code>config</code>/<code>start</code> — it works
              standalone, from anywhere, against any Expo project, talking to Docker directly. Those three commands are
              only for the optional web GUI (live dashboard, build history, keystore manager).
            </p>
          </Section>

          <Section id="commands" title="Commands">
            <div className="overflow-hidden rounded-lg border border-border">
              <table className="w-full text-left text-sm">
                <tbody>
                  {COMMANDS.map((c) => (
                    <tr key={c.cmd} className="border-b border-border last:border-0">
                      <td className="whitespace-nowrap px-4 py-3 align-top font-mono text-accent">{c.cmd}</td>
                      <td className="px-4 py-3 text-text-dim">{c.body}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
            <p>
              Run <code>ebl &lt;command&gt; --help</code> for the full option list of any command.
            </p>
          </Section>

          <Section id="build-engines" title="Build engines">
            <div className="overflow-hidden rounded-lg border border-border">
              <table className="w-full text-left text-sm">
                <thead>
                  <tr className="border-b border-border bg-surface-2 font-mono text-xs text-text-dim">
                    <th className="px-4 py-2 font-medium">Engine</th>
                    <th className="px-4 py-2 font-medium">How</th>
                    <th className="px-4 py-2 font-medium">Expo account?</th>
                  </tr>
                </thead>
                <tbody>
                  {ENGINES.map((e) => (
                    <tr key={e.name} className="border-b border-border last:border-0">
                      <td className="whitespace-nowrap px-4 py-3 align-top font-medium text-text">{e.name}</td>
                      <td className="px-4 py-3 align-top text-text-dim">{e.how}</td>
                      <td className="px-4 py-3 align-top text-text-dim">{e.account}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          </Section>

          <Section id="accounts" title="Multiple Expo accounts">
            <p>
              If your apps aren&apos;t all under the same EAS account, save one token per account instead of juggling{" "}
              <code>--expo-token</code>/<code>EXPO_TOKEN</code> by hand — <code>ebl build</code> auto-selects the right
              one by matching the project&apos;s <code>app.json</code> <code>expo.owner</code> field.
            </p>
            <p>
              Resolution order: <code>--expo-token</code>/<code>EXPO_TOKEN</code> (explicit override) → a{" "}
              <code>.ebl-token</code> file in the project root → the saved token for this project&apos;s owner → the
              default token from <code>ebl config</code>. If none resolve and the engine needs one, you&apos;re prompted
              interactively, with the option to save it to <code>.ebl-token</code> (auto-added to <code>.gitignore</code>).
            </p>
          </Section>

          <Section id="signing" title="Signing">
            <p>
              <strong>Debug</strong> — every build is signed with Expo&apos;s default debug keystore. Good for a test
              device, not accepted by the Play Store.
            </p>
            <p>
              <strong>Release</strong> — provide a real keystore (<code>.jks</code>/<code>.keystore</code>) via{" "}
              <code>--keystore</code> on the CLI, or upload once in the GUI&apos;s keystore manager. The password/alias
              are AES-256-GCM encrypted at rest and only decrypted in memory for the one build that uses them — nothing
              persists in your project folder after the build finishes.
            </p>
          </Section>

          <Section id="security" title="Security notes">
            <ul className="list-disc space-y-2 pl-5">
              <li>Both services bind to <code>127.0.0.1</code> by default.</li>
              <li>
                Anything that looks like a secret in your <code>.env</code>/<code>eas.json</code>/
                <code>google-services.json</code> is redacted from streamed and persisted build logs.
              </li>
              <li>
                <code>ebl config</code>&apos;s saved settings live at <code>~/.config/ebl/config.json</code> (0600),
                secrets AES-256-GCM-encrypted using a machine-local key that never leaves the machine.
              </li>
              <li>Every uploaded keystore stays inside Docker-managed storage or is deleted at the end of a build.</li>
            </ul>
          </Section>

          <Section id="troubleshooting" title="Troubleshooting">
            <dl className="space-y-5">
              {TROUBLESHOOTING.map((t) => (
                <div key={t.q}>
                  <dt className="font-medium text-text">{t.q}</dt>
                  <dd className="mt-1">{t.a}</dd>
                </div>
              ))}
            </dl>
          </Section>
        </div>
      </div>
    </div>
  );
}
