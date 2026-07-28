"use client";

import { useState } from "react";
import Link from "next/link";
import ThemeToggle from "./ThemeToggle";
// Plain <img>, not next/image: a small static icon doesn't need Next's on-request
// image optimization pipeline, which shells out to sharp at request time — simplest
// to just not exercise that code path at all for a fixed-size logo.

const REPO_URL = "https://github.com/41vi4p/expo-builder-local";

const links = [
  { href: "/docs", label: "Docs" },
  { href: "/download", label: "Download" },
  { href: "/about", label: "About" },
];

export default function Header() {
  const [open, setOpen] = useState(false);

  return (
    <header className="sticky top-0 z-50 border-b border-border bg-bg/80 backdrop-blur-sm">
      <div className="mx-auto flex max-w-6xl items-center justify-between px-6 py-4">
        <Link href="/" className="flex items-center gap-2.5" onClick={() => setOpen(false)}>
          {/* eslint-disable-next-line @next/next/no-img-element */}
          <img src="/ebl_logo.png" alt="" width={28} height={28} className="logo-icon h-12 w-16" />
          <span className="flex items-baseline gap-2">
            <span className="font-display text-lg font-semibold tracking-tight">
              expo<span className="text-accent">/</span>builder
            </span>
            <span className="hidden font-mono text-xs text-text-dim sm:inline">local</span>
          </span>
        </Link>

        <nav className="hidden items-center gap-6 font-mono text-sm text-text-dim md:flex">
          {links.map((l) => (
            <Link key={l.href} href={l.href} className="transition-colors hover:text-text">
              {l.label}
            </Link>
          ))}
          <a
            href={REPO_URL}
            target="_blank"
            rel="noopener noreferrer"
            className="rounded-md border border-border px-3 py-1.5 transition-colors hover:border-accent hover:text-accent"
          >
            GitHub
          </a>
          <ThemeToggle />
        </nav>

        <div className="flex items-center gap-2 md:hidden">
          <ThemeToggle />
          <button
            type="button"
            aria-label={open ? "Close menu" : "Open menu"}
            aria-expanded={open}
            onClick={() => setOpen((v) => !v)}
            className="flex h-9 w-9 items-center justify-center rounded-md border border-border text-text"
          >
            <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="currentColor" strokeWidth="1.75">
              {open ? (
                <path strokeLinecap="round" d="M6 6l12 12M18 6L6 18" />
              ) : (
                <path strokeLinecap="round" d="M4 6h16M4 12h16M4 18h16" />
              )}
            </svg>
          </button>
        </div>
      </div>

      {open && (
        <nav className="flex flex-col gap-1 border-t border-border px-6 py-4 font-mono text-sm md:hidden">
          {links.map((l) => (
            <Link
              key={l.href}
              href={l.href}
              onClick={() => setOpen(false)}
              className="rounded-md px-2 py-2.5 text-text-dim transition-colors hover:bg-surface-2 hover:text-text"
            >
              {l.label}
            </Link>
          ))}
          <a
            href={REPO_URL}
            target="_blank"
            rel="noopener noreferrer"
            className="rounded-md px-2 py-2.5 text-text-dim transition-colors hover:bg-surface-2 hover:text-text"
          >
            GitHub ↗
          </a>
        </nav>
      )}
    </header>
  );
}
