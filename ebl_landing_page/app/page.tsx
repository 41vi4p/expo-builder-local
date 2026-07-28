import Link from "next/link";
import Hero3DLoader from "@/components/Hero3DLoader";
import TerminalTypewriter from "@/components/TerminalTypewriter";
import PhasePipeline from "@/components/PhasePipeline";
import FeatureGrid from "@/components/FeatureGrid";

export default function HomePage() {
  return (
    <div>
      {/* ---------------------------------------------------------------- hero */}
      <section className="overflow-hidden border-b border-border">
        <div className="mx-auto grid max-w-6xl grid-cols-1 items-center gap-4 px-6 pb-12 pt-16 sm:pb-16 sm:pt-20 lg:grid-cols-[1.1fr_1fr] lg:gap-8 lg:pb-20">
          <div>
            <span className="phase-tag">hero</span>
            <h1 className="mt-3 font-display text-4xl font-semibold tracking-tight sm:text-5xl md:text-6xl">
              Build Android apps.
              <br />
              Skip the cloud queue.
            </h1>
            <p className="mt-5 max-w-md text-base text-text-dim sm:text-lg">
              Turn a managed Expo project into a signed APK or AAB entirely on your own
              machine — one disposable Docker container, no Expo account required.
            </p>
            <div className="mt-8 flex flex-wrap items-center gap-3">
              <Link
                href="/download"
                className="rounded-md bg-accent px-5 py-2.5 text-sm font-medium text-white transition-opacity hover:opacity-90"
              >
                Get started
              </Link>
              <Link
                href="/docs"
                className="rounded-md border border-border px-5 py-2.5 text-sm font-medium text-text transition-colors hover:border-accent hover:text-accent"
              >
                Read the docs
              </Link>
            </div>
            <div className="mt-10 max-w-sm">
              <TerminalTypewriter />
            </div>
          </div>
          <div className="pointer-events-none h-[280px] sm:h-[360px] lg:h-[520px]">
            <Hero3DLoader />
          </div>
        </div>
      </section>

      {/* --------------------------------------------------------- how it works */}
      <section className="mx-auto max-w-6xl px-6 py-16 sm:py-20">
        <span className="phase-tag">how-it-works</span>
        <h2 className="mt-3 font-display text-2xl font-semibold tracking-tight sm:text-3xl">
          What actually happens when you run <code className="font-mono text-accent">ebl build .</code>
        </h2>
        <p className="mt-2 max-w-2xl text-sm text-text-dim sm:text-base">
          The same six phases, whether you watch them scroll by in a terminal or on the GUI&apos;s live dashboard.
        </p>
        <div className="mt-10">
          <PhasePipeline />
        </div>
      </section>

      {/* ------------------------------------------------------------- features */}
      <section className="mx-auto max-w-6xl px-6 py-16 sm:py-20">
        <span className="phase-tag">why-this</span>
        <h2 className="mt-3 font-display text-2xl font-semibold tracking-tight sm:text-3xl">
          Built for the way Expo apps actually ship
        </h2>
        <div className="mt-10">
          <FeatureGrid />
        </div>
      </section>

      {/* ------------------------------------------------------------------ cta */}
      <section className="border-t border-border">
        <div className="mx-auto flex max-w-6xl flex-col items-start gap-6 px-6 py-16 sm:flex-row sm:items-center sm:justify-between sm:py-20">
          <div>
            <span className="phase-tag">collect</span>
            <h2 className="mt-3 font-display text-2xl font-semibold tracking-tight sm:text-3xl">
              Your first signed APK is a few minutes away.
            </h2>
          </div>
          <div className="flex flex-wrap items-center gap-3">
            <Link
              href="/download"
              className="rounded-md bg-accent px-5 py-2.5 text-sm font-medium text-white transition-opacity hover:opacity-90"
            >
              Download ebl
            </Link>
            <Link
              href="/docs"
              className="rounded-md border border-border px-5 py-2.5 text-sm font-medium text-text transition-colors hover:border-accent hover:text-accent"
            >
              Setup instructions
            </Link>
          </div>
        </div>
      </section>
    </div>
  );
}
