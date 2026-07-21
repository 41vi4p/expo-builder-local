"use client";

import { useEffect, useState } from "react";

/** Tracks whether the chart surface is currently dark — combining the OS
 * `prefers-color-scheme` media query with this app's own `data-theme` toggle on
 * <html> (see globals.css), so a manual light/dark override always wins over the OS
 * setting, in both directions. Chart series colors are picked per-mode (see
 * ResourceCharts) rather than left to an automatic CSS flip, per the dataviz skill's
 * "dark mode is selected, not automatic" rule. */
export function useIsDarkMode(): boolean {
  const [isDark, setIsDark] = useState(false);

  useEffect(() => {
    const media = window.matchMedia("(prefers-color-scheme: dark)");
    const root = document.documentElement;

    const compute = () => {
      const theme = root.getAttribute("data-theme");
      if (theme === "dark") return true;
      if (theme === "light") return false;
      return media.matches;
    };

    const update = () => setIsDark(compute());
    update();

    media.addEventListener("change", update);
    const observer = new MutationObserver(update);
    observer.observe(root, { attributes: true, attributeFilter: ["data-theme"] });

    return () => {
      media.removeEventListener("change", update);
      observer.disconnect();
    };
  }, []);

  return isDark;
}
