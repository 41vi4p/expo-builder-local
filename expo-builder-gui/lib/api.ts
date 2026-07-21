import type {
  BuildPhase,
  BuildRecord,
  DirEntry,
  ExpoProjectInfo,
  KeystoreRecord,
  StartBuildRequest,
  StatSample,
} from "./types";

// The browser talks to the orchestrator directly (not proxied through Next.js) — set
// this at build/deploy time to wherever the orchestrator service is reachable from the
// developer's machine, e.g. http://localhost:4001 for the default docker-compose setup.
export const ORCHESTRATOR_URL =
  process.env.NEXT_PUBLIC_ORCHESTRATOR_URL ?? "http://localhost:4001";

export function wsUrl(path: string): string {
  return ORCHESTRATOR_URL.replace(/^http/, "ws") + path;
}

class ApiError extends Error {
  constructor(message: string, public status: number) {
    super(message);
  }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const res = await fetch(`${ORCHESTRATOR_URL}${path}`, {
    ...init,
    headers: { "Content-Type": "application/json", ...init?.headers },
  });
  if (!res.ok) {
    let message = res.statusText;
    try {
      const body = await res.json();
      message = body.error ?? message;
    } catch {
      // non-JSON error body — fall back to statusText
    }
    throw new ApiError(message, res.status);
  }
  if (res.status === 204) return undefined as T;
  return res.json();
}

export { ApiError };

// --- filesystem browser --------------------------------------------------------

export function listRoots(): Promise<{ roots: string[] }> {
  return request("/api/fs/roots");
}

export function listDir(path: string): Promise<{
  path: string;
  parent: string | null;
  entries: DirEntry[];
  project: ExpoProjectInfo;
}> {
  return request(`/api/fs/list?path=${encodeURIComponent(path)}`);
}

// --- builds ----------------------------------------------------------------------

export function startBuild(body: StartBuildRequest): Promise<{ build: BuildRecord }> {
  return request("/api/builds", { method: "POST", body: JSON.stringify(body) });
}

export function listBuilds(limit = 100): Promise<{ builds: BuildRecord[] }> {
  return request(`/api/builds?limit=${limit}`);
}

export function getBuild(id: string): Promise<{ build: BuildRecord; phases: BuildPhase[] }> {
  return request(`/api/builds/${id}`);
}

export function getBuildStats(id: string): Promise<{ samples: StatSample[] }> {
  return request(`/api/builds/${id}/stats`);
}

export function cancelBuild(id: string): Promise<{ cancelled: boolean }> {
  return request(`/api/builds/${id}/cancel`, { method: "POST" });
}

export function artifactUrl(id: string): string {
  return `${ORCHESTRATOR_URL}/api/builds/${id}/artifact`;
}

export function logUrl(id: string): string {
  return `${ORCHESTRATOR_URL}/api/builds/${id}/log`;
}

// --- keystores ---------------------------------------------------------------------

export function listKeystores(): Promise<{ keystores: KeystoreRecord[] }> {
  return request("/api/keystores");
}

export async function uploadKeystore(form: FormData): Promise<KeystoreRecord> {
  const res = await fetch(`${ORCHESTRATOR_URL}/api/keystores`, { method: "POST", body: form });
  if (!res.ok) {
    const body = await res.json().catch(() => ({}));
    throw new ApiError(body.error ?? res.statusText, res.status);
  }
  return res.json();
}

export function deleteKeystore(id: string): Promise<void> {
  return request(`/api/keystores/${id}`, { method: "DELETE" });
}
