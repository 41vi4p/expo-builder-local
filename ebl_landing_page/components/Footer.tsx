import Link from "next/link";

const REPO_URL = "https://github.com/41vi4p/expo-builder-local";

export default function Footer() {
  return (
    <footer className="border-t border-border">
      <div className="mx-auto flex max-w-6xl flex-col gap-4 px-6 py-8 text-sm text-text-dim sm:flex-row sm:items-center sm:justify-between">
        <div className="flex items-baseline gap-2">
          <span className="font-display font-semibold text-text">
            expo<span className="text-accent">/</span>builder
          </span>
          <span className="font-mono text-xs">local</span>
        </div>
        <nav className="flex flex-wrap gap-x-5 gap-y-2 font-mono">
          <Link href="/docs" className="transition-colors hover:text-text">
            Docs
          </Link>
          <Link href="/download" className="transition-colors hover:text-text">
            Download
          </Link>
          <Link href="/about" className="transition-colors hover:text-text">
            About
          </Link>
          <a href={REPO_URL} target="_blank" rel="noopener noreferrer" className="transition-colors hover:text-text">
            GitHub
          </a>
        </nav>
        <p className="font-mono text-xs">GPL-3.0 · not affiliated with Expo/Google</p>
      </div>
    </footer>
  );
}
