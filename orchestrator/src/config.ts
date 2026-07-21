import path from 'node:path';
import fs from 'node:fs';

function required(name: string, fallback?: string): string {
  const v = process.env[name] ?? fallback;
  if (v === undefined) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return v;
}

function bool(name: string, fallback: boolean): boolean {
  const v = process.env[name];
  if (v === undefined) return fallback;
  return v === '1' || v.toLowerCase() === 'true';
}

// Directories the built-in directory browser is allowed to expose, colon-separated
// (e.g. ALLOWED_ROOTS=/home/dev/code:/home/dev/repos).
//
// IMPORTANT: these must be the *host's real paths*, and docker-compose.yml must bind-mount
// each one into this container at that exact same path (not renamed to /host/... or
// similar). This container talks to the Docker daemon over the mounted host socket —
// it's a sibling container, not a nested one — so every path this process hands to
// dockerode's Binds is resolved by the daemon against the real host filesystem. If the
// path inside this container didn't match the host path, the runner container's bind
// mount would silently point at the wrong (or a nonexistent) host directory.
const allowedRoots = (process.env.ALLOWED_ROOTS ?? '')
  .split(':')
  .map((p) => p.trim())
  .filter(Boolean)
  .map((p) => path.resolve(p));

const dataDir = required('DATA_DIR', '/data');
for (const dir of [dataDir, path.join(dataDir, 'keystores'), path.join(dataDir, 'logs')]) {
  fs.mkdirSync(dir, { recursive: true });
}

export const config = {
  port: Number(process.env.PORT ?? 4001),
  host: process.env.HOST ?? '0.0.0.0',
  corsOrigin: process.env.CORS_ORIGIN ?? '*',

  dataDir,
  dbPath: path.join(dataDir, 'builder.db'),
  keystoreDir: path.join(dataDir, 'keystores'),
  logDir: path.join(dataDir, 'logs'),

  allowedRoots,

  dockerSocket: process.env.DOCKER_SOCKET ?? '/var/run/docker.sock',
  runnerImage: process.env.RUNNER_IMAGE ?? 'expo-builder-local-runner:latest',
  gradleCacheVolume: process.env.GRADLE_CACHE_VOLUME ?? 'expo-builder-gradle-cache',
  npmCacheVolume: process.env.NPM_CACHE_VOLUME ?? 'expo-builder-npm-cache',

  // UID/GID the runner container writes build output as, so artifacts land in the host
  // project folder owned by the developer rather than root. Defaults to 1000:1000
  // (the runner image's built-in "builder" user) — override via .env to match your host user.
  buildUid: Number(process.env.HOST_UID ?? 1000),
  buildGid: Number(process.env.HOST_GID ?? 1000),

  // Symmetric key used to encrypt keystore passwords at rest (AES-256-GCM). Must be a
  // 32-byte value, base64 or hex. Generate with: openssl rand -base64 32
  masterKey: required('MASTER_KEY'),

  // Optional default EXPO_TOKEN if the developer wants "auto" engine to prefer EAS
  // without entering a token per build. Per-build tokens (if supplied via the API)
  // always take precedence.
  defaultExpoToken: process.env.EXPO_TOKEN,

  maxConcurrentBuilds: Number(process.env.MAX_CONCURRENT_BUILDS ?? 1),
  statsIntervalMs: Number(process.env.STATS_INTERVAL_MS ?? 1000),

  allowInsecureNoAuth: bool('ALLOW_INSECURE_NO_AUTH', true),
};

export type Config = typeof config;
