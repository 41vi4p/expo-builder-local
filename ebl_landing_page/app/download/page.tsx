import type { Metadata } from "next";
import CodeBlock from "@/components/CodeBlock";
import OSTabs from "@/components/OSTabs";

export const metadata: Metadata = {
  title: "Download — expo-builder-local",
  description: "Install or uninstall the ebl CLI on Linux or Windows.",
};

const RELEASES_URL = "https://github.com/41vi4p/expo-builder-local/releases";

const REQUIREMENTS = [
  {
    label: "RAM",
    value: "16 GB recommended",
    detail: "8 GB works but is tight — the Gradle and Kotlin compiler daemons alone reserve up to 2 GB of heap each during a build, the same ballpark Android Studio itself recommends.",
  },
  {
    label: "CPU",
    value: "4+ cores recommended",
    detail: "Gradle and Kotlin annotation processing both parallelize across cores — more cores means a noticeably faster compile phase.",
  },
  {
    label: "Disk space",
    value: "20+ GB free",
    detail: "The runner image alone is ~6.8 GB; the shared Gradle/npm caches grow to 5+ GB after a few builds (and speed up every build after the first).",
  },
  {
    label: "OS & Docker",
    value: "Linux, or Windows 10/11 (WSL2)",
    detail: "Docker Engine on Linux, or Docker Desktop on Windows — either way, it needs to be installed and running before you start.",
  },
] as const;

function SystemRequirements() {
  return (
    <section className="mt-8 rounded-lg border border-border bg-surface-2 p-6">
      <h2 className="font-display text-base font-semibold">System requirements</h2>
      <p className="mt-1 text-sm text-text-dim">
        Building an Android app locally means running the same Gradle/Kotlin/Android SDK toolchain Android Studio
        does &mdash; these aren&apos;t hard limits ebl enforces, just realistic guidance so your first build isn&apos;t
        a slow, swap-thrashing surprise.
      </p>
      <dl className="mt-5 grid grid-cols-1 gap-5 sm:grid-cols-2">
        {REQUIREMENTS.map((r) => (
          <div key={r.label}>
            <dt className="font-mono text-xs text-text-dim">{r.label}</dt>
            <dd className="mt-0.5 font-display text-sm font-semibold">{r.value}</dd>
            <dd className="mt-1 text-sm text-text-dim">{r.detail}</dd>
          </div>
        ))}
      </dl>
    </section>
  );
}

function StepHeading({ n, title }: { n: number; title: string }) {
  return (
    <div className="flex items-center gap-3">
      <span className="flex h-7 w-7 items-center justify-center rounded-full border border-border font-mono text-xs text-text-dim">
        {n}
      </span>
      <h3 className="font-display text-lg font-semibold">{title}</h3>
    </div>
  );
}

function LinuxInstall() {
  return (
    <div className="space-y-10">
      <section>
        <StepHeading n={1} title="APT repository (recommended)" />
        <p className="mt-3 text-sm text-text-dim">
          A real, GPG-signed APT repo &mdash; once added, <code className="font-mono text-accent">sudo apt upgrade</code>{" "}
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
        <StepHeading n={2} title="One-line installer" />
        <p className="mt-3 text-sm text-text-dim">
          Adds the APT repo for you where possible, otherwise falls back to a direct <code className="font-mono text-accent">.deb</code> download.
        </p>
        <div className="mt-4">
          <CodeBlock label="bash" code="curl -fsSL https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/install.sh | sh" />
        </div>
      </section>

      <section>
        <StepHeading n={3} title="Direct .deb download" />
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
          View releases on GitHub &#8599;
        </a>
      </section>
    </div>
  );
}

function WindowsInstall() {
  return (
    <div className="space-y-10">
      <p className="text-sm text-text-dim">
        ebl doesn&apos;t talk to Docker natively on Windows &mdash; Docker Desktop for Windows already runs on a WSL2
        backend by default, so builds are Linux either way. Windows support is a thin wrapper: a small{" "}
        <code className="font-mono text-accent">ebl.exe</code>{" "}
        launcher forwards commands into your WSL2 distro&apos;s real, unmodified
        Linux <code className="font-mono text-accent">ebl</code>.
      </p>

      <section className="rounded-lg border border-accent/40 bg-accent-soft p-6">
        <h3 className="font-display text-base font-semibold">
          Prerequisite: install{" "}
          <a href="https://www.docker.com/products/docker-desktop/" target="_blank" rel="noopener noreferrer" className="text-accent hover:underline">
            Docker Desktop
          </a>{" "}
          yourself, first
        </h3>
        <p className="mt-2 text-sm text-text-dim">
          Neither installer below installs Docker Desktop for you &mdash; it&apos;s a much heavier install with its
          own license/reboot considerations. Both check for it up front and stop with a clear message if it&apos;s
          missing, rather than silently setting up WSL2/<code className="font-mono text-accent">ebl</code>{" "}
          for a Docker daemon that isn&apos;t there yet.
        </p>
      </section>

      <section>
        <StepHeading n={1} title="One-line installer (PowerShell)" />
        <p className="mt-3 text-sm text-text-dim">
          Checks for Docker Desktop, checks/installs WSL2 and a distro if needed, installs{" "}
          <code className="font-mono text-accent">ebl</code> inside it, and puts{" "}
          <code className="font-mono text-accent">ebl.exe</code> on your PATH.
        </p>
        <div className="mt-4">
          <CodeBlock label="powershell" code="irm https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/windows/install.ps1 | iex" />
        </div>
      </section>

      <section>
        <StepHeading n={2} title="Or the GUI installer" />
        <p className="mt-3 text-sm text-text-dim">
          Download <code className="font-mono text-accent">ebl-setup.exe</code>{" "}
          and run it &mdash; a thin Inno Setup wrapper around the same install script, with a familiar Windows
          installer UI and an entry in{" "}
          <em>Add or Remove Programs</em>.
        </p>
        <a
          href={RELEASES_URL}
          target="_blank"
          rel="noopener noreferrer"
          className="mt-4 inline-flex rounded-md border border-border px-4 py-2 text-sm font-medium transition-colors hover:border-accent hover:text-accent"
        >
          Download ebl-setup.exe &#8599;
        </a>
      </section>

      <section className="rounded-lg border border-border bg-surface-2 p-6">
        <h3 className="font-display text-base font-semibold">One more one-time step</h3>
        <p className="mt-2 text-sm text-text-dim">
          Once installed, open Docker Desktop and enable{" "}
          <strong className="text-text">Settings &rarr; Resources &rarr; WSL Integration</strong>{" "}
          for the distro the installer used (both installers print which one at the end) &mdash; that&apos;s what
          makes Docker actually reachable from inside it.
        </p>
      </section>
    </div>
  );
}

function LinuxUninstall() {
  return (
    <div className="space-y-4 text-sm text-text-dim">
      <p>If installed via the APT repo or a `.deb`:</p>
      <CodeBlock label="bash" code={`sudo apt remove ebl\n# and, if you added it: sudo rm /etc/apt/sources.list.d/ebl.list`} />
      <p>
        This removes the <code className="font-mono text-accent">ebl</code> binary only &mdash; your projects,{" "}
        <code className="font-mono text-accent">ebl_builds/</code> artifacts, and{" "}
        <code className="font-mono text-accent">~/.config/ebl/</code> (saved tokens/settings) are untouched. Remove
        that config directory yourself for a completely clean slate:
      </p>
      <CodeBlock label="bash" code="rm -rf ~/.config/ebl" />
    </div>
  );
}

function WindowsUninstall() {
  return (
    <div className="space-y-4 text-sm text-text-dim">
      <p>
        If you used the <strong className="text-text">one-line/PowerShell install</strong>, run the uninstaller
        script it left behind &mdash; removes the launcher and its PATH entry, and asks whether to also remove the
        real <code className="font-mono text-accent">ebl</code> package from inside WSL:
      </p>
      <CodeBlock label="powershell" code={String.raw`& "$env:LOCALAPPDATA\Programs\ebl\uninstall.ps1"`} />
      <p>
        If you used the <strong className="text-text">ebl-setup.exe GUI installer</strong>, uninstall it the normal
        Windows way instead &mdash; <em>Settings &rarr; Apps &rarr; ebl (expo-local-builder) &rarr; Uninstall</em>, or
        from <em>Add or Remove Programs</em>. That removes the launcher and PATH entry only (no interactive prompt
        fits an uninstaller GUI flow) &mdash; run the script above with{" "}
        <code className="font-mono text-accent">-RemoveFromWsl</code> afterward if you also want the package gone
        from inside WSL.
      </p>
      <p>Either way, WSL2 and Docker Desktop themselves are left alone &mdash; they&apos;re your system&apos;s own components, not ebl&apos;s.</p>
    </div>
  );
}

export default function DownloadPage() {
  return (
    <div className="mx-auto max-w-3xl px-6 py-12 sm:py-16">
      <span className="phase-tag">download</span>
      <h1 className="mt-3 font-display text-3xl font-semibold tracking-tight sm:text-4xl">Get ebl</h1>
      <p className="mt-2 text-text-dim">Linux and Windows (via WSL2) are both supported. Pick your platform:</p>

      <SystemRequirements />

      <div className="mt-10">
        <OSTabs linux={<LinuxInstall />} windows={<WindowsInstall />} />
      </div>

      <section className="mt-10 rounded-lg border border-border bg-surface-2 p-6">
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
          No <code className="font-mono text-accent">ebl config</code>/<code className="font-mono text-accent">ebl start</code>{" "}
          required for a first build &mdash; see the{" "}
          <a href="/docs" className="text-accent hover:underline">Docs</a> for the web GUI and every option.
        </p>
      </section>

      <div className="mt-16">
        <span className="phase-tag">uninstall</span>
        <h2 className="mt-3 font-display text-2xl font-semibold tracking-tight">Uninstall</h2>
        <div className="mt-8">
          <OSTabs linux={<LinuxUninstall />} windows={<WindowsUninstall />} />
        </div>
      </div>
    </div>
  );
}
