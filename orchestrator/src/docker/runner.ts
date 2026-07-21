import Docker from 'dockerode';
import type { Readable } from 'node:stream';
import { config } from '../config';
import type { ArtifactType, Engine, SigningMode } from '../types';

export const docker = new Docker({ socketPath: config.dockerSocket });

let volumesReady: Promise<void> | null = null;
/** Creates the shared npm/Gradle cache volumes once, if they don't already exist —
 * reused across every build so dependency/Gradle downloads aren't repeated per run. */
export function ensureCacheVolumes(): Promise<void> {
  if (!volumesReady) {
    volumesReady = (async () => {
      for (const name of [config.gradleCacheVolume, config.npmCacheVolume]) {
        try {
          await docker.createVolume({ Name: name });
        } catch (err: any) {
          if (err?.statusCode !== 409) throw err; // 409 = already exists, fine
        }
      }
    })();
  }
  return volumesReady;
}

export interface RunnerParams {
  appPath: string; // absolute host path to the Expo project root
  artifactType: ArtifactType;
  profile: string;
  engine: Engine;
  signingMode: SigningMode;
  expoToken?: string;
  keystore?: {
    hostPath: string; // absolute host path to the uploaded keystore file
    filename: string;
    storePassword: string;
    keyAlias: string;
    keyPassword: string;
  };
}

/** Creates (but does not start) a runner container for one build. Returns the
 * dockerode Container handle; caller is responsible for start/attach/wait/remove. */
export async function createRunnerContainer(params: RunnerParams): Promise<Docker.Container> {
  await ensureCacheVolumes();

  const env: string[] = [
    'APP_DIR=/work/app',
    `ARTIFACT_TYPE=${params.artifactType}`,
    `PROFILE=${params.profile}`,
    `ENGINE=${params.engine}`,
    `SIGNING_MODE=${params.signingMode}`,
    `BUILD_UID=${config.buildUid}`,
    `BUILD_GID=${config.buildGid}`,
  ];
  const expoToken = params.expoToken ?? config.defaultExpoToken;
  if (expoToken) env.push(`EXPO_TOKEN=${expoToken}`);

  const binds = [
    `${params.appPath}:/work/app`,
    `${config.gradleCacheVolume}:/cache/gradle`,
    `${config.npmCacheVolume}:/cache/npm`,
  ];

  if (params.signingMode === 'release' && params.keystore) {
    const containerKeystorePath = `/keystores/${params.keystore.filename}`;
    binds.push(`${params.keystore.hostPath}:${containerKeystorePath}:ro`);
    env.push(
      `KEYSTORE_PATH=${containerKeystorePath}`,
      `KEYSTORE_PASSWORD=${params.keystore.storePassword}`,
      `KEY_ALIAS=${params.keystore.keyAlias}`,
      `KEY_PASSWORD=${params.keystore.keyPassword}`
    );
  }

  return docker.createContainer({
    Image: config.runnerImage,
    Env: env,
    HostConfig: {
      Binds: binds,
      AutoRemove: false, // we remove explicitly after finalizing the build record
      // No network restrictions: npm/gradle/eas all need outbound internet. If your
      // environment requires egress control, add a Docker network policy at the host
      // level rather than here.
    },
    Tty: true, // gives Gradle a real TTY so `--console=rich` emits live "NN% EXECUTING"
    OpenStdin: false,
    AttachStdout: true,
    AttachStderr: true,
    WorkingDir: '/work/app',
  });
}

/** Attaches to a started container's combined stdout/stderr stream. Because the
 * container has Tty:true, Docker does NOT multiplex stdout/stderr with the 8-byte
 * frame header — this is a plain byte stream, safe to read directly. */
export async function attachContainerOutput(container: Docker.Container): Promise<NodeJS.ReadableStream> {
  const stream = await container.attach({ stream: true, stdout: true, stderr: true });
  return stream as unknown as NodeJS.ReadableStream;
}

/** Opens the container's live resource-usage stream (CPU/mem/net/blkio), one JSON
 * object per line, at roughly Docker's default ~1s sampling interval. */
export async function attachContainerStats(container: Docker.Container): Promise<Readable> {
  const stream = await container.stats({ stream: true });
  return stream as unknown as Readable;
}

export async function stopContainer(container: Docker.Container): Promise<void> {
  try {
    await container.stop({ t: 5 });
  } catch (err: any) {
    if (err?.statusCode !== 304 && err?.statusCode !== 404) throw err; // already stopped/gone
  }
}

export async function removeContainer(container: Docker.Container): Promise<void> {
  try {
    await container.remove({ force: true });
  } catch (err: any) {
    if (err?.statusCode !== 404) throw err;
  }
}
