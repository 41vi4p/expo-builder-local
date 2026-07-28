import type { Metadata } from "next";
import CodeBlock from "@/components/CodeBlock";

export const metadata: Metadata = {
  title: "Download — expo-builder-local",
  description: "Install the ebl CLI via APT, a one-line script, or a direct .deb download.",
};

const RELEASES_URL = "https://github.com/41vi4p/expo-builder-local/releases";

export default function DownloadPage() {
  return (
    <div className="mx-auto max-w-3xl px-6 py-12 sm:py-16">
      <span className="phase-tag">download</span>
      <h1 className="mt-3 font-display text-3xl font-semibold tracking-tight sm:text-4xl">Get ebl</h1>
      <p className="mt-2 text-text-dim">
        Linux (Ubuntu/Debian) only for now. Pick whichever install path fits — all three end up with the same{" "}
        <code className="font-mono text-accent">ebl</code> command.
      </p>

      <div className="mt-10 space-y-10">
        <section>
          <div className="flex items-center gap-3">
            <span className="flex h-7 w-7 items-center justify-center rounded-full border border-border font-mono text-xs text-text-dim">1</span>
            <h2 className="font-display text-lg font-semibold">APT repository (recommended)</h2>
          </div>
          <p className="mt-3 text-sm text-text-dim">
            A real, GPG-signed APT repo — once added, <code className="font-mono text-accent">sudo apt upgrade</code>{" "}
            picks up new releases automatically.
          </p>
          <div className="mt-4">
            <CodeBlock
              label="bash"
              code={`curl -fsSL https://41vi4p.github.io/expo-builder-local/apt/pubkey.gpg | sudo gpg --dearmor -o /usr/share/keyrings/ebl-archive-keyring.gpg
echo "deb [signed-by=/usr/share/keyrings/ebl-archive-keyring.gpg] https://41vi4p.github.io/expo-builder-local/apt stable main" | sudo tee /etc/apt/sources.list.d/ebl.list
sudo apt update && sudo apt install ebl`}
            />
          </div>
        </section>

        <section>
          <div className="flex items-center gap-3">
            <span className="flex h-7 w-7 items-center justify-center rounded-full border border-border font-mono text-xs text-text-dim">2</span>
            <h2 className="font-display text-lg font-semibold">One-line installer</h2>
          </div>
          <p className="mt-3 text-sm text-text-dim">
            Adds the APT repo for you where possible, otherwise falls back to a direct <code className="font-mono text-accent">.deb</code> download.
          </p>
          <div className="mt-4">
            <CodeBlock label="bash" code="curl -fsSL https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/install.sh | sh" />
          </div>
        </section>

        <section>
          <div className="flex items-center gap-3">
            <span className="flex h-7 w-7 items-center justify-center rounded-full border border-border font-mono text-xs text-text-dim">3</span>
            <h2 className="font-display text-lg font-semibold">Direct .deb download</h2>
          </div>
          <p className="mt-3 text-sm text-text-dim">
            Grab <code className="font-mono text-accent">ebl_*_amd64.deb</code> from GitHub Releases, then:
          </p>
          <div className="mt-4">
            <CodeBlock label="bash" code="sudo apt install ./ebl_*_amd64.deb" />
          </div>
          <a
            href={RELEASES_URL}
            target="_blank"
            rel="noopener noreferrer"
            className="mt-4 inline-flex rounded-md border border-border px-4 py-2 text-sm font-medium transition-colors hover:border-accent hover:text-accent"
          >
            View releases on GitHub ↗
          </a>
        </section>

        <section className="rounded-lg border border-border bg-surface-2 p-6">
          <h2 className="font-display text-lg font-semibold">After installing</h2>
          <div className="mt-4">
            <CodeBlock
              label="bash"
              code={`ebl setup     # checks/installs Docker, pulls the runner image
cd /path/to/your/expo/app
ebl build .   # signed APK in ./ebl_builds/`}
            />
          </div>
          <p className="mt-4 text-sm text-text-dim">
            No <code className="font-mono text-accent">ebl config</code>/<code className="font-mono text-accent">ebl start</code> required for a first
            build — see the <a href="/docs" className="text-accent hover:underline">Docs</a> for the web GUI and every option.
          </p>
        </section>
      </div>
    </div>
  );
}
