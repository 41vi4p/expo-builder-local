import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';

export interface ArtifactMetrics {
  sizeBytes: number;
  sha256: string;
  versionName: string | null;
  versionCode: string | null;
  applicationId: string | null;
  gitCommit: string | null;
  gitBranch: string | null;
}

/**
 * Everything here reads from the host-mounted app directory directly (the orchestrator
 * shares the same bind mount the runner container built in) rather than shelling into
 * Android tooling — so this container never needs the Android SDK, just Node + git.
 */

function sha256File(filePath: string): Promise<string> {
  return new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha256');
    const stream = fs.createReadStream(filePath);
    stream.on('data', (chunk) => hash.update(chunk));
    stream.on('end', () => resolve(hash.digest('hex')));
    stream.on('error', reject);
  });
}

/** Pulls applicationId/versionCode/versionName out of the Gradle project that was
 * actually used for this build — more reliable than re-deriving from app.json, since
 * expo prebuild is the source of truth for what ended up in the compiled artifact. */
function readGradleManifestValues(appPath: string): {
  applicationId: string | null;
  versionCode: string | null;
  versionName: string | null;
} {
  const buildGradlePath = path.join(appPath, 'android', 'app', 'build.gradle');
  if (!fs.existsSync(buildGradlePath)) {
    return { applicationId: null, versionCode: null, versionName: null };
  }
  const src = fs.readFileSync(buildGradlePath, 'utf8');
  const applicationId = src.match(/applicationId\s+["']([^"']+)["']/)?.[1] ?? null;
  const versionCode = src.match(/versionCode\s+(\d+)/)?.[1] ?? null;
  const versionName = src.match(/versionName\s+["']([^"']+)["']/)?.[1] ?? null;
  return { applicationId, versionCode, versionName };
}

function readGit(appPath: string): { commit: string | null; branch: string | null } {
  try {
    const commit = execFileSync('git', ['rev-parse', '--short', 'HEAD'], {
      cwd: appPath,
      stdio: ['ignore', 'pipe', 'ignore'],
    })
      .toString()
      .trim();
    const branch = execFileSync('git', ['rev-parse', '--abbrev-ref', 'HEAD'], {
      cwd: appPath,
      stdio: ['ignore', 'pipe', 'ignore'],
    })
      .toString()
      .trim();
    return { commit, branch };
  } catch {
    return { commit: null, branch: null }; // not a git repo, or git unavailable — non-fatal
  }
}

export async function extractArtifactMetrics(appPath: string, artifactPath: string): Promise<ArtifactMetrics> {
  const stat = fs.statSync(artifactPath);
  const sha256 = await sha256File(artifactPath);
  const gradleValues = readGradleManifestValues(appPath);
  const git = readGit(appPath);

  let versionName = gradleValues.versionName;
  if (!versionName) {
    try {
      versionName = JSON.parse(fs.readFileSync(path.join(appPath, 'package.json'), 'utf8')).version ?? null;
    } catch {
      versionName = null;
    }
  }

  return {
    sizeBytes: stat.size,
    sha256,
    versionName,
    versionCode: gradleValues.versionCode,
    applicationId: gradleValues.applicationId,
    gitCommit: git.commit,
    gitBranch: git.branch,
  };
}
