import Database from 'better-sqlite3';
import { config } from '../config';
import type {
  BuildRecord,
  BuildPhase,
  StatSample,
  KeystoreRecord,
  KeystoreSecret,
} from '../types';

const db = new Database(config.dbPath);
db.pragma('journal_mode = WAL');
db.pragma('foreign_keys = ON');

db.exec(`
CREATE TABLE IF NOT EXISTS builds (
  id TEXT PRIMARY KEY,
  app_path TEXT NOT NULL,
  app_name TEXT,
  profile TEXT NOT NULL,
  artifact_type TEXT NOT NULL,
  engine_requested TEXT NOT NULL,
  engine_resolved TEXT,
  signing_mode TEXT NOT NULL,
  keystore_id TEXT,
  status TEXT NOT NULL,
  current_phase TEXT,
  current_phase_label TEXT,
  progress INTEGER NOT NULL DEFAULT 0,
  error TEXT,
  container_id TEXT,
  created_at INTEGER NOT NULL,
  started_at INTEGER,
  finished_at INTEGER,
  duration_seconds INTEGER,
  eta_seconds INTEGER,
  artifact_path TEXT,
  artifact_size_bytes INTEGER,
  artifact_sha256 TEXT,
  build_number INTEGER,
  version_name TEXT,
  version_code TEXT,
  application_id TEXT,
  git_commit TEXT,
  git_branch TEXT,
  previous_artifact_size_bytes INTEGER
);

CREATE TABLE IF NOT EXISTS build_phases (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  build_id TEXT NOT NULL REFERENCES builds(id) ON DELETE CASCADE,
  phase TEXT NOT NULL,
  label TEXT NOT NULL,
  started_at INTEGER NOT NULL,
  ended_at INTEGER
);
CREATE INDEX IF NOT EXISTS idx_build_phases_build_id ON build_phases(build_id);

CREATE TABLE IF NOT EXISTS stat_samples (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  build_id TEXT NOT NULL REFERENCES builds(id) ON DELETE CASCADE,
  ts INTEGER NOT NULL,
  cpu_pct REAL NOT NULL,
  mem_mb REAL NOT NULL,
  mem_pct REAL NOT NULL,
  net_rx_bytes INTEGER NOT NULL,
  net_tx_bytes INTEGER NOT NULL,
  blk_read_bytes INTEGER NOT NULL,
  blk_write_bytes INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_stat_samples_build_id ON stat_samples(build_id);

CREATE TABLE IF NOT EXISTS keystores (
  id TEXT PRIMARY KEY,
  name TEXT NOT NULL,
  filename TEXT NOT NULL,
  storage_path TEXT NOT NULL,
  key_alias TEXT NOT NULL,
  store_password_enc TEXT NOT NULL,
  key_password_enc TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
`);

// --- row <-> record mapping -------------------------------------------------

function rowToBuild(row: any): BuildRecord {
  return {
    id: row.id,
    appPath: row.app_path,
    appName: row.app_name,
    profile: row.profile,
    artifactType: row.artifact_type,
    engineRequested: row.engine_requested,
    engineResolved: row.engine_resolved,
    signingMode: row.signing_mode,
    keystoreId: row.keystore_id,
    status: row.status,
    currentPhase: row.current_phase,
    currentPhaseLabel: row.current_phase_label,
    progress: row.progress,
    error: row.error,
    containerId: row.container_id,
    createdAt: row.created_at,
    startedAt: row.started_at,
    finishedAt: row.finished_at,
    durationSeconds: row.duration_seconds,
    etaSeconds: row.eta_seconds,
    artifactPath: row.artifact_path,
    artifactSizeBytes: row.artifact_size_bytes,
    artifactSha256: row.artifact_sha256,
    buildNumber: row.build_number,
    versionName: row.version_name,
    versionCode: row.version_code,
    applicationId: row.application_id,
    gitCommit: row.git_commit,
    gitBranch: row.git_branch,
    previousArtifactSizeBytes: row.previous_artifact_size_bytes,
  };
}

function rowToPhase(row: any): BuildPhase {
  return { phase: row.phase, label: row.label, startedAt: row.started_at, endedAt: row.ended_at };
}

function rowToStat(row: any): StatSample {
  return {
    ts: row.ts,
    cpuPct: row.cpu_pct,
    memMb: row.mem_mb,
    memPct: row.mem_pct,
    netRxBytes: row.net_rx_bytes,
    netTxBytes: row.net_tx_bytes,
    blkReadBytes: row.blk_read_bytes,
    blkWriteBytes: row.blk_write_bytes,
  };
}

function rowToKeystoreRecord(row: any): KeystoreRecord {
  return { id: row.id, name: row.name, filename: row.filename, keyAlias: row.key_alias, createdAt: row.created_at };
}

function rowToKeystoreSecret(row: any): KeystoreSecret {
  return {
    ...rowToKeystoreRecord(row),
    storagePath: row.storage_path,
    storePasswordEnc: row.store_password_enc,
    keyPasswordEnc: row.key_password_enc,
  };
}

// --- builds ------------------------------------------------------------------

export function insertBuild(build: BuildRecord): void {
  db.prepare(
    `INSERT INTO builds (
      id, app_path, app_name, profile, artifact_type, engine_requested, engine_resolved,
      signing_mode, keystore_id, status, current_phase, current_phase_label, progress,
      error, container_id, created_at, started_at, finished_at, duration_seconds,
      eta_seconds, artifact_path, artifact_size_bytes, artifact_sha256, build_number, version_name,
      version_code, application_id, git_commit, git_branch, previous_artifact_size_bytes
    ) VALUES (
      @id, @appPath, @appName, @profile, @artifactType, @engineRequested, @engineResolved,
      @signingMode, @keystoreId, @status, @currentPhase, @currentPhaseLabel, @progress,
      @error, @containerId, @createdAt, @startedAt, @finishedAt, @durationSeconds,
      @etaSeconds, @artifactPath, @artifactSizeBytes, @artifactSha256, @buildNumber, @versionName,
      @versionCode, @applicationId, @gitCommit, @gitBranch, @previousArtifactSizeBytes
    )`
  ).run(build);
}

const buildColumnMap: Record<string, string> = {
  appName: 'app_name',
  engineResolved: 'engine_resolved',
  status: 'status',
  currentPhase: 'current_phase',
  currentPhaseLabel: 'current_phase_label',
  progress: 'progress',
  error: 'error',
  containerId: 'container_id',
  startedAt: 'started_at',
  finishedAt: 'finished_at',
  durationSeconds: 'duration_seconds',
  etaSeconds: 'eta_seconds',
  artifactPath: 'artifact_path',
  artifactSizeBytes: 'artifact_size_bytes',
  artifactSha256: 'artifact_sha256',
  buildNumber: 'build_number',
  versionName: 'version_name',
  versionCode: 'version_code',
  applicationId: 'application_id',
  gitCommit: 'git_commit',
  gitBranch: 'git_branch',
  previousArtifactSizeBytes: 'previous_artifact_size_bytes',
};

export function updateBuild(id: string, patch: Partial<BuildRecord>): void {
  const sets: string[] = [];
  const params: Record<string, unknown> = { id };
  for (const [key, value] of Object.entries(patch)) {
    const column = buildColumnMap[key];
    if (!column) continue;
    sets.push(`${column} = @${key}`);
    params[key] = value;
  }
  if (sets.length === 0) return;
  db.prepare(`UPDATE builds SET ${sets.join(', ')} WHERE id = @id`).run(params);
}

export function getBuild(id: string): BuildRecord | null {
  const row = db.prepare('SELECT * FROM builds WHERE id = ?').get(id);
  return row ? rowToBuild(row) : null;
}

export function listBuilds(limit = 100): BuildRecord[] {
  const rows = db.prepare('SELECT * FROM builds ORDER BY created_at DESC LIMIT ?').all(limit);
  return rows.map(rowToBuild);
}

/** Most recent successful build for the same app+profile+artifactType, excluding `excludeId` —
 * used to compute the size delta shown in the metrics panel and to seed ETA estimates. */
export function previousSuccessfulBuild(
  appPath: string,
  profile: string,
  artifactType: string,
  excludeId: string
): BuildRecord | null {
  const row = db
    .prepare(
      `SELECT * FROM builds
       WHERE app_path = ? AND profile = ? AND artifact_type = ? AND status = 'success' AND id != ?
       ORDER BY created_at DESC LIMIT 1`
    )
    .get(appPath, profile, artifactType, excludeId);
  return row ? rowToBuild(row) : null;
}

/** Median duration of past successful builds for the same app+profile — used to blend
 * a stable ETA rather than relying solely on percent-complete extrapolation. */
export function medianDurationSeconds(appPath: string, profile: string): number | null {
  const rows = db
    .prepare(
      `SELECT duration_seconds FROM builds
       WHERE app_path = ? AND profile = ? AND status = 'success' AND duration_seconds IS NOT NULL
       ORDER BY created_at DESC LIMIT 10`
    )
    .all(appPath, profile) as { duration_seconds: number }[];
  if (rows.length === 0) return null;
  const sorted = rows.map((r) => r.duration_seconds).sort((a, b) => a - b);
  const mid = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0 ? (sorted[mid - 1] + sorted[mid]) / 2 : sorted[mid];
}

// --- phases --------------------------------------------------------------------

export function insertPhase(buildId: string, phase: string, label: string, startedAt: number): void {
  db.prepare('INSERT INTO build_phases (build_id, phase, label, started_at) VALUES (?, ?, ?, ?)').run(
    buildId,
    phase,
    label,
    startedAt
  );
}

export function endOpenPhases(buildId: string, endedAt: number): void {
  db.prepare('UPDATE build_phases SET ended_at = ? WHERE build_id = ? AND ended_at IS NULL').run(
    endedAt,
    buildId
  );
}

export function listPhases(buildId: string): BuildPhase[] {
  const rows = db
    .prepare('SELECT * FROM build_phases WHERE build_id = ? ORDER BY id ASC')
    .all(buildId);
  return rows.map(rowToPhase);
}

// --- stats -----------------------------------------------------------------------

export function insertStatSample(buildId: string, s: StatSample): void {
  db.prepare(
    `INSERT INTO stat_samples (
      build_id, ts, cpu_pct, mem_mb, mem_pct, net_rx_bytes, net_tx_bytes, blk_read_bytes, blk_write_bytes
    ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)`
  ).run(buildId, s.ts, s.cpuPct, s.memMb, s.memPct, s.netRxBytes, s.netTxBytes, s.blkReadBytes, s.blkWriteBytes);
}

export function listStatSamples(buildId: string): StatSample[] {
  const rows = db
    .prepare('SELECT * FROM stat_samples WHERE build_id = ? ORDER BY ts ASC')
    .all(buildId);
  return rows.map(rowToStat);
}

// --- keystores ---------------------------------------------------------------

export function insertKeystore(record: KeystoreSecret): void {
  db.prepare(
    `INSERT INTO keystores (id, name, filename, storage_path, key_alias, store_password_enc, key_password_enc, created_at)
     VALUES (@id, @name, @filename, @storagePath, @keyAlias, @storePasswordEnc, @keyPasswordEnc, @createdAt)`
  ).run(record);
}

export function listKeystores(): KeystoreRecord[] {
  const rows = db.prepare('SELECT * FROM keystores ORDER BY created_at DESC').all();
  return rows.map(rowToKeystoreRecord);
}

export function getKeystoreSecret(id: string): KeystoreSecret | null {
  const row = db.prepare('SELECT * FROM keystores WHERE id = ?').get(id);
  return row ? rowToKeystoreSecret(row) : null;
}

export function deleteKeystore(id: string): void {
  db.prepare('DELETE FROM keystores WHERE id = ?').run(id);
}

export default db;
