// Mirrors orchestrator/src/types.ts — kept as a plain duplicate rather than a shared
// package since the two services deploy independently; if you change one, change both.

export type ArtifactType = "apk" | "aab";
export type Engine = "auto" | "eas" | "gradle";
export type ResolvedEngine = "eas" | "gradle";
export type SigningMode = "debug" | "release";

export type BuildStatus =
  | "queued"
  | "starting"
  | "running"
  | "success"
  | "failed"
  | "cancelled";

export interface BuildPhase {
  phase: string;
  label: string;
  startedAt: number;
  endedAt: number | null;
}

export interface BuildRecord {
  id: string;
  appPath: string;
  appName: string | null;
  profile: string;
  artifactType: ArtifactType;
  engineRequested: Engine;
  engineResolved: ResolvedEngine | null;
  signingMode: SigningMode;
  keystoreId: string | null;
  status: BuildStatus;
  currentPhase: string | null;
  currentPhaseLabel: string | null;
  progress: number;
  error: string | null;
  containerId: string | null;
  createdAt: number;
  startedAt: number | null;
  finishedAt: number | null;
  durationSeconds: number | null;
  etaSeconds: number | null;
  artifactPath: string | null;
  artifactSizeBytes: number | null;
  artifactSha256: string | null;
  versionName: string | null;
  versionCode: string | null;
  applicationId: string | null;
  gitCommit: string | null;
  gitBranch: string | null;
  previousArtifactSizeBytes: number | null;
}

export interface StatSample {
  ts: number;
  cpuPct: number;
  memMb: number;
  memPct: number;
  netRxBytes: number;
  netTxBytes: number;
  blkReadBytes: number;
  blkWriteBytes: number;
}

export interface KeystoreRecord {
  id: string;
  name: string;
  filename: string;
  keyAlias: string;
  createdAt: number;
}

export interface ExpoProjectInfo {
  isExpoProject: boolean;
  name?: string;
  version?: string;
  androidPackage?: string;
  androidVersionCode?: number | string;
  easProfiles?: string[];
  hasGoogleServicesJson?: boolean;
  hasEnvFile?: boolean;
  reason?: string;
}

export interface DirEntry {
  name: string;
  path: string;
  isDirectory: boolean;
}

export interface StartBuildRequest {
  appPath: string;
  artifactType: ArtifactType;
  profile: string;
  engine: Engine;
  signingMode: SigningMode;
  keystoreId?: string;
  expoToken?: string;
}

export type BuildWsMessage =
  | { type: "snapshot"; build: BuildRecord; phases: BuildPhase[]; log: string; stats: StatSample[] }
  | { type: "log"; line: string }
  | { type: "phase"; phase: string; label: string }
  | { type: "progress"; percent: number; etaSeconds: number | null }
  | { type: "stats"; sample: StatSample }
  | { type: "status"; status: BuildStatus; build?: BuildRecord }
  | { type: "error"; message: string };
