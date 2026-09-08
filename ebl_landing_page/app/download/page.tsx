import type { Metadata } from "next";
import CodeBlock from "@/components/CodeBlock";
import OSTabs from "@/components/OSTabs";

export const metadata: Metadata = {
  title: "Download - expo-builder-local",
  description: "Install or uninstall the ebl CLI on Linux (Debian/Ubuntu or Arch-based) or Windows.",
};

const RELEASES_URL = "https://github.com/41vi4p/expo-builder-local/releases";

const REQUIREMENTS = [
  {
    label: "RAM",
    value: "16 GB+ recommended",
    detail: "A cold build compiles native code for 4 CPU architectures plus the Kotlin/JS toolchain - genuinely heavy. Less can still work, but on Windows specifically, Docker Desktop's WSL2 VM running out of memory crashes its own Engine API outright rather than just slowing down; the installer sizes its memory/swap limits automatically from your actual RAM to reduce that.",
  },
  {
    label: "CPU",
    value: "4+ cores recommended",
    detail: "Gradle and Kotlin annotation processing both parallelize across cores - more cores means a noticeably faster compile phase.",
  },
  {
    label: "Disk space",
    value: "~40 GB free",
    detail: "Covers the runner image (~6.8 GB), Gradle/npm caches that grow over a few builds, and - on Windows - WSL2's swap file headroom. Reclaim all of it any time with ebl clean --all.",
  },
  {
    label: "OS & Docker",
    value: "Linux, or Windows 10/11",
    detail: "Docker Engine on Linux, or Docker Desktop on Windows - either way, it needs to be installed and running before you start. On Windows, ebl.exe itself talks to Docker Desktop directly and needs no WSL2 distro of its own, but Docker Desktop's own default backend is a WSL2 VM, which is what actually runs your builds.",
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
        <StepHeading n={1} title="APT repository (recommended - Debian/Ubuntu)" />
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
        <StepHeading n={2} title="One-line installer (any distro)" />
        <p className="mt-3 text-sm text-text-dim">
          On Debian/Ubuntu, adds the APT repo for you where possible, otherwise falls back to a direct{" "}
          <code className="font-mono text-accent">.deb</code> download. On <strong className="text-text">Arch-based
          distros</strong> (pacman detected), it instead builds{" "}
          <code className="font-mono text-accent">packaging/arch/PKGBUILD</code> from source with{" "}
          <code className="font-mono text-accent">makepkg</code>, giving a real pacman-tracked package &mdash;{" "}
          <code className="font-mono text-accent">pacman -Qi ebl</code>/<code className="font-mono text-accent">pacman -R ebl</code>{" "}
          both work normally afterward. Requires the <code className="font-mono text-accent">base-devel</code> group
          and a non-root user (<code className="font-mono text-accent">makepkg</code> refuses to run as root) &mdash;
          if either is missing, it falls back to the plain tarball below instead.
        </p>
        <div className="mt-4">
          <CodeBlock label="bash" code="curl -fsSL https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/install.sh | sh" />
        </div>
      </section>

      <section>
        <StepHeading n={3} title="Arch package, built by hand" />
        <p className="mt-3 text-sm text-text-dim">
          Same PKGBUILD the one-line installer uses above &mdash; run it yourself if you&apos;d rather review it
          first. Not published on the AUR yet.
        </p>
        <div className="mt-4">
          <CodeBlock
            label="bash"
            code={`curl -fsSLO https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/packaging/arch/PKGBUILD
makepkg -si`}
          />
        </div>
      </section>

      <section>
        <StepHeading n={4} title="Direct .deb download (Debian/Ubuntu)" />
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
        platform uses, with <strong className="text-text">two build engines</strong> to choose from &mdash; Docker
        Desktop/WSL2 was, until now, the only option on Windows, and it&apos;s the single biggest source of Windows
        friction (VM overhead, WSL2 memory tuning, Docker Desktop&apos;s own licensing/install).{" "}
        <strong className="text-text">Native mode is the default</strong>: it installs the Android SDK, JDK 17, and
        Node.js directly on this machine (isolated under <code className="font-mono text-accent">%LOCALAPPDATA%\ebl</code>,
        never touching an existing install) and runs builds as real processes on your system &mdash; no Docker
        Desktop or WSL2 at all.
      </p>

      <section className="rounded-lg border border-accent/40 bg-accent-soft p-6">
        <h3 className="font-display text-base font-semibold">⚠️ Native mode is unverified on real Windows hardware</h3>
        <p className="mt-2 text-sm text-text-dim">
          It was built with no Windows machine available to test it on &mdash; written to mirror the Docker
          engine&apos;s exact behavior and checked wherever possible, but it hasn&apos;t actually run a real build
          yet. Docker mode is the original, actually-used-in-production engine if you&apos;d rather not be the first
          to find native mode&apos;s rough edges (pass <code className="font-mono text-accent">-Mode Docker</code>{" "}
          / <code className="font-mono text-accent">--runtime docker</code>). Please{" "}
          <a
            href="https://github.com/41vi4p/expo-builder-local/issues"
            target="_blank"
            rel="noopener noreferrer"
            className="text-accent hover:underline"
          >
            report
          </a>{" "}
          anything that doesn&apos;t work.
        </p>
      </section>

      <section className="overflow-hidden rounded-lg border border-border">
        <table className="w-full text-left text-sm">
          <thead>
            <tr className="border-b border-border">
              <th className="px-4 py-3 font-display font-semibold"></th>
              <th className="px-4 py-3 font-display font-semibold">Native (default)</th>
              <th className="px-4 py-3 font-display font-semibold">Docker</th>
            </tr>
          </thead>
          <tbody className="text-text-dim">
            <tr className="border-b border-border">
              <td className="px-4 py-3 font-mono text-xs">Requires</td>
              <td className="px-4 py-3">Nothing extra &mdash; installs its own JDK/SDK/Node</td>
              <td className="px-4 py-3">Docker Desktop (+ usually WSL2)</td>
            </tr>
            <tr className="border-b border-border">
              <td className="px-4 py-3 font-mono text-xs">Isolation</td>
              <td className="px-4 py-3">Runs directly on your system</td>
              <td className="px-4 py-3">Disposable, fully isolated Linux container</td>
            </tr>
            <tr>
              <td className="px-4 py-3 font-mono text-xs">Status</td>
              <td className="px-4 py-3">New, unverified on real hardware</td>
              <td className="px-4 py-3">The original engine &mdash; what Linux/macOS also use</td>
            </tr>
          </tbody>
        </table>
      </section>

      <section>
        <StepHeading n={1} title="One-line installer (PowerShell)" />
        <p className="mt-3 text-sm text-text-dim">
          Installs in Native mode by default, puts <code className="font-mono text-accent">ebl.exe</code> on your
          PATH, then runs <code className="font-mono text-accent">ebl setup --runtime native</code> for you &mdash;
          that&apos;s where the JDK/Android SDK/Node downloads actually happen, so a fresh install takes a while.
        </p>
        <div className="mt-4">
          <CodeBlock label="powershell" code="irm https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/windows/install.ps1 | iex" />
        </div>
        <p className="mt-3 text-sm text-text-dim">
          Prefer Docker instead? Pass <code className="font-mono text-accent">-Mode Docker</code> &mdash; this also
          checks for Docker Desktop and sizes WSL2&apos;s memory/swap limits from your actual installed RAM, since
          its 50%-of-host default is routinely too little for a real Android build:
        </p>
        <div className="mt-4">
          <CodeBlock
            label="powershell"
            code={`irm https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/windows/install.ps1 -OutFile install.ps1
.\\install.ps1 -Mode Docker`}
          />
        </div>
      </section>

      <section>
        <StepHeading n={2} title="Or the GUI installer" />
        <p className="mt-3 text-sm text-text-dim">
          Grab <code className="font-mono text-accent">ebl-setup-*.exe</code>{" "}
          and run it &mdash; a wizard page lets you pick Native or Docker (Native pre-selected), then it runs the
          same install script under the hood, with a familiar Windows installer UI and an entry in{" "}
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
        Switch modes later any time with <code className="font-mono text-accent">ebl setup --runtime docker|native</code>.
        Docker mode: if disk space gets tight afterward (build caches, the runner image, WSL2&apos;s swap file),
        reclaim it any time with <code className="font-mono text-accent">ebl clean --all</code>.
      </p>
    </div>
  );
}

function LinuxUninstall() {
  return (
    <div className="space-y-4 text-sm text-text-dim">
      <p>If installed via the APT repo or a `.deb`:</p>
      <CodeBlock label="bash" code={`sudo apt remove ebl\n# and, if you added it: sudo rm /etc/apt/sources.list.d/ebl.list`} />
      <p>If installed on an Arch-based distro (via the one-line installer or the PKGBUILD directly):</p>
      <CodeBlock label="bash" code="sudo pacman -R ebl" />
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
        script it left behind &mdash; from a terminal, so it can interactively ask about anything beyond the
        always-safe removal (<code className="font-mono text-accent">ebl.exe</code> + its PATH entry):
      </p>
      <CodeBlock label="powershell" code={String.raw`& "$env:LOCALAPPDATA\Programs\ebl\uninstall.ps1"`} />
      <p>
        For a Native-mode install, it also offers to remove the Android SDK/JDK/Node toolchain it downloaded
        &mdash; only ever the components it actually installed itself; anything it detected and reused instead is
        never touched. Pass <code className="font-mono text-accent">-Quiet</code> to skip those prompts.
      </p>
      <p>
        If you used the <strong className="text-text">ebl-setup-*.exe GUI installer</strong>, uninstall it the normal
        Windows way instead &mdash; <em>Settings &rarr; Apps &rarr; ebl (expo-local-builder) &rarr; Uninstall</em>, or
        from <em>Add or Remove Programs</em>. It always runs non-interactively, so it only does the always-safe
        removal &mdash; run <code className="font-mono text-accent">uninstall.ps1</code> directly from a terminal
        instead for the toolchain/config cleanup prompts.
      </p>
      <p>
        Either way, Docker Desktop itself is left alone (Docker-mode installs) &mdash; it&apos;s your system&apos;s
        own component, not ebl&apos;s. <code className="font-mono text-accent">.wslconfig</code>&apos;s WSL2
        memory/swap tuning is also left as-is.
      </p>
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
