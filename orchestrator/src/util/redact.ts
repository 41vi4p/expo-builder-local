import fs from 'node:fs';
import path from 'node:path';

/**
 * Every one of these apps' repos ships real credentials committed to `.env` and
 * `eas.json` (Firebase API keys, a PhonePe client secret under an EXPO_PUBLIC_ prefix,
 * etc). None of that is this tool's to fix, but a build log is not the place for it to
 * leak either — build container stdout, the persisted log file, and the WS stream can
 * all end up shared (screenshots, copy-pasted into a chat, etc).
 *
 * `Redactor` masks any known secret value wherever it appears in a line of output.
 * It is intentionally a plain substring replace (not regex-escaped-once-and-cached)
 * over a short, per-build list, so it stays fast on the hot log-streaming path.
 */
export type Redactor = (line: string) => string;

const MIN_SECRET_LENGTH = 6;

export function makeRedactor(secrets: string[]): Redactor {
  const unique = Array.from(new Set(secrets.filter((s) => s && s.length >= MIN_SECRET_LENGTH)));
  // Longest-first so a secret that is a prefix/substring of another is fully masked.
  unique.sort((a, b) => b.length - a.length);
  if (unique.length === 0) {
    return (line) => line;
  }
  return (line: string) => {
    let out = line;
    for (const secret of unique) {
      if (out.includes(secret)) {
        out = out.split(secret).join('***REDACTED***');
      }
    }
    return out;
  };
}

/** Extracts candidate secret values from an app's committed `.env` and `eas.json` so
 * they can be redacted from that app's own build logs (see makeRedactor above). */
export function collectAppSecrets(appPath: string, profile: string): string[] {
  const secrets: string[] = [];

  for (const envFile of ['.env', '.env.local']) {
    const p = path.join(appPath, envFile);
    if (!fs.existsSync(p)) continue;
    try {
      const content = fs.readFileSync(p, 'utf8');
      for (const line of content.split('\n')) {
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith('#')) continue;
        const eq = trimmed.indexOf('=');
        if (eq === -1) continue;
        let value = trimmed.slice(eq + 1).trim();
        value = value.replace(/^['"]|['"]$/g, '');
        if (value) secrets.push(value);
      }
    } catch {
      // best-effort — a malformed .env just means fewer redactions, not a build failure
    }
  }

  const easJsonPath = path.join(appPath, 'eas.json');
  if (fs.existsSync(easJsonPath)) {
    try {
      const easJson = JSON.parse(fs.readFileSync(easJsonPath, 'utf8'));
      const profileEnv = easJson?.build?.[profile]?.env;
      if (profileEnv && typeof profileEnv === 'object') {
        for (const v of Object.values(profileEnv)) {
          if (typeof v === 'string' && v) secrets.push(v);
        }
      }
    } catch {
      // ignore malformed eas.json — same rationale as above
    }
  }

  return secrets;
}
