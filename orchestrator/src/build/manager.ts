import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import type Docker from 'dockerode';
import { config } from '../config';
import { buildHub } from '../ws/hub';
import { makeRedactor, collectAppSecrets } from '../util/redact';
import { LineBuffer } from '../util/lineBuffer';
import { ProgressTracker } from './progress';
import { extractArtifactMetrics } from './metrics';
import { detectExpoProject, isPathAllowed } from './detect';
import {
  createRunnerContainer,
  attachContainerOutput,
  attachContainerStats,
  stopContainer,
  removeContainer,
} from '../docker/runner';
import { parseDockerStats } from '../docker/stats';
import * as db from '../store/db';
import { decrypt } from '../util/crypto';
import type { BuildRecord, StartBuildRequest, ResolvedEngine } from '../types';

export class ValidationError extends Error {}

const activeContainers = new Map<string, Docker.Container>();
const cancelledBuilds = new Set<string>();
const queue: string[] = [];
let runningCount = 0;

export function logFilePath(buildId: string): string {
  return path.join(config.logDir, `${buildId}.log`);
}

export function readLogFile(buildId: string): string {
  const p = logFilePath(buildId);
  return fs.existsSync(p) ? fs.readFileSync(p, 'utf8') : '';
}

/** Validates the request, persists a `queued` build record, and enqueues it for
 * launch (immediately, if under MAX_CONCURRENT_BUILDS). Returns the created record. */
export function startBuild(req: StartBuildRequest): BuildRecord {
  const appPath = path.resolve(req.appPath);
  if (!isPathAllowed(appPath)) {
    throw new ValidationError(`Path is outside the configured allowed roots: ${appPath}`);
  }
  const info = detectExpoProject(appPath);
  if (!info.isExpoProject) {
    throw new ValidationError(info.reason ?? 'Not a recognizable Expo project');
  }
  if (req.signingMode === 'release' && req.keystoreId && !db.getKeystoreSecret(req.keystoreId)) {
    throw new ValidationError('Selected keystore was not found');
  }

  const now = Date.now();
  const build: BuildRecord = {
    id: crypto.randomUUID(),
    appPath,
    appName: info.name ?? null,
    profile: req.profile,
    artifactType: req.artifactType,
    engineRequested: req.engine,
    engineResolved: null,
    signingMode: req.signingMode,
    keystoreId: req.keystoreId ?? null,
    status: 'queued',
    currentPhase: null,
    currentPhaseLabel: null,
    progress: 0,
    error: null,
    containerId: null,
    createdAt: now,
    startedAt: null,
    finishedAt: null,
    durationSeconds: null,
    etaSeconds: null,
    artifactPath: null,
    artifactSizeBytes: null,
    artifactSha256: null,
    buildNumber: null,
    versionName: null,
    versionCode: null,
    applicationId: null,
    gitCommit: null,
    gitBranch: null,
    previousArtifactSizeBytes: null,
  };
  db.insertBuild(build);
  fs.writeFileSync(logFilePath(build.id), '');

  // Stash the per-build secret material (expoToken, keystore passwords) the launcher
  // needs, keyed by id — never persisted to the DB, only held in memory until launch.
  pendingSecrets.set(build.id, { expoToken: req.expoToken });

  queue.push(build.id);
  processQueue();
  return build;
}

interface PendingSecrets {
  expoToken?: string;
}
const pendingSecrets = new Map<string, PendingSecrets>();

function processQueue(): void {
  while (runningCount < config.maxConcurrentBuilds && queue.length > 0) {
    const id = queue.shift()!;
    if (cancelledBuilds.has(id)) continue;
    runningCount++;
    launchBuild(id).finally(() => {
      runningCount--;
      processQueue();
    });
  }
}

export function cancelBuild(buildId: string): boolean {
  const build = db.getBuild(buildId);
  if (!build) return false;
  if (build.status === 'queued') {
    cancelledBuilds.add(buildId);
    db.updateBuild(buildId, { status: 'cancelled', finishedAt: Date.now() });
    buildHub.publish(buildId, { type: 'status', status: 'cancelled' });
    return true;
  }
  if (build.status === 'starting' || build.status === 'running') {
    cancelledBuilds.add(buildId);
    const container = activeContainers.get(buildId);
    if (container) void stopContainer(container);
    return true;
  }
  return false;
}

async function launchBuild(buildId: string): Promise<void> {
  const build = db.getBuild(buildId);
  if (!build) return;
  const secrets = pendingSecrets.get(buildId) ?? {};
  pendingSecrets.delete(buildId);

  const startedAt = Date.now();
  db.updateBuild(buildId, { status: 'starting', startedAt });
  buildHub.publish(buildId, { type: 'status', status: 'starting' });

  const logStream = fs.createWriteStream(logFilePath(buildId), { flags: 'a' });

  const tracker = new ProgressTracker(build.appPath, build.profile);
  const lineBuffer = new LineBuffer();
  let resolvedEngine: ResolvedEngine | null = null;
  let artifactPath: string | null = null;
  let lastError: string | null = null;
  let buildNumber: number | null = null;

  let keystoreParam: Parameters<typeof createRunnerContainer>[0]['keystore'];
  if (build.signingMode === 'release' && build.keystoreId) {
    const secret = db.getKeystoreSecret(build.keystoreId);
    if (!secret) {
      finalizeFailed(buildId, startedAt, 'Selected keystore was deleted before the build started');
      return;
    }
    keystoreParam = {
      hostPath: secret.storagePath,
      filename: secret.filename,
      storePassword: decrypt(secret.storePasswordEnc),
      keyAlias: secret.keyAlias,
      keyPassword: decrypt(secret.keyPasswordEnc),
    };
  }
  // Redactor covers: the app's own committed .env/eas.json secrets, any EXPO_TOKEN
  // supplied for this build, and the decrypted keystore passwords (if any) — nothing
  // sensitive should reach the persisted log file or the WS stream unmasked.
  const fullRedactor = makeRedactor([
    ...collectAppSecrets(build.appPath, build.profile),
    ...(secrets.expoToken ? [secrets.expoToken] : []),
    ...(keystoreParam ? [keystoreParam.storePassword, keystoreParam.keyPassword] : []),
  ]);
  const appendLogRedacted = (text: string) => {
    const redacted = fullRedactor(text);
    logStream.write(redacted);
    buildHub.publish(buildId, { type: 'log', line: redacted });
  };

  let container: Docker.Container;
  try {
    container = await createRunnerContainer({
      appPath: build.appPath,
      artifactType: build.artifactType,
      profile: build.profile,
      engine: build.engineRequested,
      signingMode: build.signingMode,
      expoToken: secrets.expoToken,
      keystore: keystoreParam,
    });
  } catch (err: any) {
    finalizeFailed(buildId, startedAt, `Failed to create build container: ${err?.message ?? err}`);
    return;
  }

  activeContainers.set(buildId, container);
  db.updateBuild(buildId, { containerId: container.id, status: 'running' });
  buildHub.publish(buildId, { type: 'status', status: 'running' });

  const handleLines = (chunkText: string) => {
    for (const line of lineBuffer.push(chunkText)) {
      if (line.startsWith('@@ENGINE:')) {
        resolvedEngine = line.slice('@@ENGINE:'.length).trim() as ResolvedEngine;
        tracker.setEngine(resolvedEngine);
        db.updateBuild(buildId, { engineResolved: resolvedEngine });
        continue;
      }
      if (line.startsWith('@@ARTIFACT:')) {
        artifactPath = line.slice('@@ARTIFACT:'.length).trim();
        continue;
      }
      if (line.startsWith('@@BUILD_NUMBER:')) {
        buildNumber = Number(line.slice('@@BUILD_NUMBER:'.length).trim());
        continue;
      }
      if (line.startsWith('@@ERROR:')) {
        lastError = line.slice('@@ERROR:'.length).trim();
        continue;
      }
      if (line.startsWith('@@DURATION:')) {
        continue; // we compute duration ourselves from startedAt/finishedAt
      }
      for (const evt of tracker.handleLine(line)) {
        if (evt.type === 'phase') {
          db.endOpenPhases(buildId, Date.now());
          db.insertPhase(buildId, evt.phase, evt.label, Date.now());
          db.updateBuild(buildId, { currentPhase: evt.phase, currentPhaseLabel: evt.label });
          buildHub.publish(buildId, { type: 'phase', phase: evt.phase, label: evt.label });
        } else {
          db.updateBuild(buildId, { progress: Math.round(evt.percent), etaSeconds: evt.etaSeconds });
          buildHub.publish(buildId, { type: 'progress', percent: Math.round(evt.percent), etaSeconds: evt.etaSeconds });
        }
      }
    }
  };

  try {
    // Attach output *before* starting the container so nothing printed in the first
    // instant is missed; dockerode's attach on a created-but-not-yet-started
    // container simply waits and then streams from the moment it starts.
    const outputStream = await attachContainerOutput(container);
    outputStream.on('data', (chunk: Buffer) => {
      const text = chunk.toString('utf8');
      appendLogRedacted(text);
      handleLines(text);
    });

    await container.start();

    // Stats, unlike logs, can only be requested once the container is actually
    // running — the Docker API has nothing to stream before that.
    const statsStream = await attachContainerStats(container);
    let statsBuffer = '';
    statsStream.on('data', (chunk: Buffer) => {
      statsBuffer += chunk.toString('utf8');
      let idx: number;
      while ((idx = statsBuffer.indexOf('\n')) !== -1) {
        const line = statsBuffer.slice(0, idx);
        statsBuffer = statsBuffer.slice(idx + 1);
        if (!line.trim()) continue;
        try {
          const sample = parseDockerStats(JSON.parse(line));
          if (sample) {
            db.insertStatSample(buildId, sample);
            buildHub.publish(buildId, { type: 'stats', sample });
          }
        } catch {
          // one malformed stats frame shouldn't kill the stream — skip it
        }
      }
    });
    statsStream.on('error', () => {
      /* stats are best-effort telemetry; a stream error shouldn't fail the build */
    });

    const waitResult = await container.wait();
    const exitCode = waitResult?.StatusCode ?? 1;

    for (const line of lineBuffer.flush()) handleLines(line + '\n');

    const finishedAt = Date.now();
    db.endOpenPhases(buildId, finishedAt);

    if (cancelledBuilds.has(buildId)) {
      db.updateBuild(buildId, { status: 'cancelled', finishedAt, durationSeconds: Math.round((finishedAt - startedAt) / 1000) });
      buildHub.publish(buildId, { type: 'status', status: 'cancelled' });
    } else if (exitCode === 0 && artifactPath) {
      await finalizeSuccess(buildId, build, startedAt, finishedAt, artifactPath, buildNumber);
    } else {
      finalizeFailed(
        buildId,
        startedAt,
        lastError ?? `Build process exited with status ${exitCode}`,
        finishedAt
      );
    }
  } catch (err: any) {
    finalizeFailed(buildId, startedAt, `Unexpected build error: ${err?.message ?? err}`);
  } finally {
    logStream.end();
    activeContainers.delete(buildId);
    cancelledBuilds.delete(buildId);
    await removeContainer(container!).catch(() => {});
  }
}

async function finalizeSuccess(
  buildId: string,
  build: BuildRecord,
  startedAt: number,
  finishedAt: number,
  artifactPath: string,
  buildNumber: number | null
): Promise<void> {
  const previous = db.previousSuccessfulBuild(build.appPath, build.profile, build.artifactType, buildId);
  try {
    const metrics = await extractArtifactMetrics(build.appPath, artifactPath);
    db.updateBuild(buildId, {
      status: 'success',
      progress: 100,
      etaSeconds: 0,
      finishedAt,
      durationSeconds: Math.round((finishedAt - startedAt) / 1000),
      artifactPath,
      buildNumber,
      artifactSizeBytes: metrics.sizeBytes,
      artifactSha256: metrics.sha256,
      versionName: metrics.versionName,
      versionCode: metrics.versionCode,
      applicationId: metrics.applicationId,
      gitCommit: metrics.gitCommit,
      gitBranch: metrics.gitBranch,
      previousArtifactSizeBytes: previous?.artifactSizeBytes ?? null,
    });
    buildHub.publish(buildId, { type: 'status', status: 'success', build: db.getBuild(buildId)! });
  } catch (err: any) {
    finalizeFailed(buildId, startedAt, `Build succeeded but metrics extraction failed: ${err?.message ?? err}`, finishedAt);
  }
}

function finalizeFailed(buildId: string, startedAt: number, message: string, finishedAt = Date.now()): void {
  db.updateBuild(buildId, {
    status: 'failed',
    error: message,
    finishedAt,
    durationSeconds: Math.round((finishedAt - startedAt) / 1000),
  });
  buildHub.publish(buildId, { type: 'error', message });
  buildHub.publish(buildId, { type: 'status', status: 'failed', build: db.getBuild(buildId)! });
}
