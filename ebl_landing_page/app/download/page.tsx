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
    value: "16 GB+ recommended",
    detail: "A cold build compiles native code for 4 CPU architectures plus the Kotlin/JS toolchain — genuinely heavy. Less can still work, but on Windows specifically, Docker Desktop's WSL2 VM running out of memory crashes its own Engine API outright rather than just slowing down; the installer sizes its memory/swap limits automatically from your actual RAM to reduce that.",
  },
  {
    label: "CPU",
    value: "4+ cores recommended",
    detail: "Gradle and Kotlin annotation processing both parallelize across cores — more cores means a noticeably faster compile phase.",
  },
  {
    label: "Disk space",
    value: "~40 GB free",
    detail: "Covers the runner image (~6.8 GB), Gradle/npm caches that grow over a few builds, and — on Windows — WSL2's swap file headroom. Reclaim all of it any time with ebl clean --all.",
  },
  {
    label: "OS & Docker",
    value: "Linux, or Windows 10/11",
    detail: "Docker Engine on Linux, or Docker Desktop on Windows — either way, it needs to be installed and running before you start. On Windows, ebl.exe itself talks to Docker Desktop directly and needs no WSL2 distro of its own, but Docker Desktop's own default backend is a WSL2 VM, which is what actually runs your builds.",
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
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/ebl-archive-keyring.gpg] https://41vi4p.github.io/expo-builder-local/apt stable main" | sudo tee /etc/apt/sources.list.d/ebl.list
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
        <code className="font-mono text-accent">ebl.exe</code> is a native Windows build of the same CLI every other
        platform uses &mdash; it talks directly to Docker Desktop&apos;s{" "}
        <code className="font-mono text-accent">\\.\pipe\docker_engine</code> named pipe (the same endpoint{" "}
        <code className="font-mono text-accent">docker.exe</code> itself uses), so{" "}
        <code className="font-mono text-accent">ebl.exe</code> itself needs no WSL2 distro or separate Linux install.
        Docker Desktop&apos;s own default backend <em>is</em> a WSL2 VM, though, and that&apos;s what actually runs
        your builds.
      </p>

      <section className="rounded-lg border border-accent/40 bg-accent-soft p-6">
        <h3 className="font-display text-base font-semibold">Setup order</h3>
        <ol className="mt-3 list-decimal space-y-3 pl-5 text-sm text-text-dim">
          <li>
            <strong className="text-text">WSL2</strong>, if you don&apos;t already have it &mdash; in an elevated
            PowerShell or Command Prompt: <code className="font-mono text-accent">wsl --install</code>, then{" "}
            <strong className="text-text">restart your computer</strong> (required).
          </li>
          <li>
            <strong className="text-text">
              <a href="https://www.docker.com/products/docker-desktop/" target="_blank" rel="noopener noreferrer" className="text-accent hover:underline">
                Docker Desktop
              </a>
            </strong>{" "}
            &mdash; install and start it.
          </li>
          <li>
            <strong className="text-text">Run the installer</strong> below. Neither installer installs WSL2 or Docker
            Desktop for you &mdash; both need their own reboot/license handling &mdash; but both check for each up
            front and tell you exactly what&apos;s missing and how to fix it, rather than failing partway through.
          </li>
        </ol>
      </section>

      <section>
        <StepHeading n={1} title="One-line installer (PowerShell)" />
        <p className="mt-3 text-sm text-text-dim">
          Checks for WSL2 and Docker Desktop, sizes WSL2&apos;s memory/swap limits from your actual installed RAM,
          downloads and installs <code className="font-mono text-accent">ebl.exe</code>, and puts it on your PATH.
        </p>
        <div className="mt-4">
          <CodeBlock label="powershell" code="irm https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/windows/install.ps1 | iex" />
        </div>
      </section>

      <section>
        <StepHeading n={2} title="Or the GUI installer" />
        <p className="mt-3 text-sm text-text-dim">
          Grab <code className="font-mono text-accent">ebl-setup-*.exe</code>{" "}
          and run it &mdash; a thin Inno Setup wrapper that bundles the same files and runs the same install script
          under the hood, with a familiar Windows installer UI and an entry in{" "}
          <em>Add or Remove Programs</em>.
        </p>
        <a
          href={RELEASES_URL}
          target="_blank"
          rel="noopener noreferrer"
          className="mt-4 inline-flex rounded-md border border-border px-4 py-2 text-sm font-medium transition-colors hover:border-accent hover:text-accent"
        >
          Download ebl-setup-*.exe &#8599;
        </a>
      </section>

      <p className="text-sm text-text-dim">
        If disk space gets tight afterward (build caches, the runner image, WSL2&apos;s swap file), reclaim it any
        time with <code className="font-mono text-accent">ebl clean --all</code>.
      </p>
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
        script it left behind &mdash; removes <code className="font-mono text-accent">ebl.exe</code> and its PATH
        entry:
      </p>
      <CodeBlock label="powershell" code={String.raw`& "$env:LOCALAPPDATA\Programs\ebl\uninstall.ps1"`} />
      <p>
        If you used the <strong className="text-text">ebl-setup-*.exe GUI installer</strong>, uninstall it the normal
        Windows way instead &mdash; <em>Settings &rarr; Apps &rarr; ebl (expo-local-builder) &rarr; Uninstall</em>, or
        from <em>Add or Remove Programs</em>.
      </p>
      <p>Either way, Docker Desktop itself is left alone &mdash; it&apos;s your system&apos;s own component, not ebl&apos;s.</p>
    </div>
  );
}

export default function DownloadPage() {
  return (
    <div className="mx-auto max-w-3xl px-6 py-12 sm:py-16">
      <span className="phase-tag">download</span>
      <h1 className="mt-3 font-display text-3xl font-semibold tracking-tight sm:text-4xl">Get ebl</h1>
      <p className="mt-2 text-text-dim">Linux and Windows are both supported natively. Pick your platform:</p>

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
        <section className="mt-4 rounded-lg border border-accent/40 bg-accent-soft p-6">
          <h3 className="font-display text-base font-semibold">
            Run <code className="font-mono text-accent">ebl clean --all</code> first
          </h3>
          <p className="mt-2 text-sm text-text-dim">
            None of the steps below touch Docker &mdash; the runner/orchestrator/web images, the Gradle/npm cache
            volumes, and any leftover build containers all stay on disk after ebl itself is gone.{" "}
            <code className="font-mono text-accent">ebl clean --all</code> (needs Docker Desktop/Docker still
            running) removes all of that safely in one step; afterward you&apos;d have to find and remove it by hand
            with raw <code className="font-mono text-accent">docker</code> commands instead.
          </p>
        </section>
        <div className="mt-8">
          <OSTabs linux={<LinuxUninstall />} windows={<WindowsUninstall />} />
        </div>
      </div>
    </div>
  );
}
