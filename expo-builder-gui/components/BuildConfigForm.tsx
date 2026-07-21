"use client";

import { useState } from "react";
import { useRouter } from "next/navigation";
import { startBuild } from "@/lib/api";
import KeystoreManager from "./KeystoreManager";
import type { ArtifactType, Engine, ExpoProjectInfo, SigningMode } from "@/lib/types";

export default function BuildConfigForm({
  appPath,
  project,
}: {
  appPath: string;
  project: ExpoProjectInfo;
}) {
  const router = useRouter();
  const profiles = project.easProfiles?.length ? project.easProfiles : ["preview", "production"];

  const [artifactType, setArtifactType] = useState<ArtifactType>("apk");
  const [profile, setProfile] = useState(profiles.includes("preview") ? "preview" : profiles[0]);
  const [engine, setEngine] = useState<Engine>("auto");
  const [signingMode, setSigningMode] = useState<SigningMode>("debug");
  const [keystoreId, setKeystoreId] = useState<string | null>(null);
  const [expoToken, setExpoToken] = useState("");
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);

  async function handleSubmit(e: React.FormEvent) {
    e.preventDefault();
    setSubmitting(true);
    setError(null);
    try {
      const { build } = await startBuild({
        appPath,
        artifactType,
        profile,
        engine,
        signingMode,
        keystoreId: signingMode === "release" && keystoreId ? keystoreId : undefined,
        expoToken: expoToken || undefined,
      });
      router.push(`/build/${build.id}`);
    } catch (err: any) {
      setError(err.message);
      setSubmitting(false);
    }
  }

  return (
    <form onSubmit={handleSubmit} className="space-y-5 rounded-lg border border-border bg-surface p-4">
      <h3 className="font-display text-sm font-semibold">Build configuration</h3>

      <div className="grid grid-cols-2 gap-3">
        <div>
          <label className="text-xs font-medium text-text-dim">Artifact</label>
          <select
            value={artifactType}
            onChange={(e) => setArtifactType(e.target.value as ArtifactType)}
            className="mt-1 w-full rounded-md border border-border bg-surface-2 px-3 py-2 text-sm"
          >
            <option value="apk">APK (install directly)</option>
            <option value="aab">AAB (Play Store bundle)</option>
          </select>
        </div>
        <div>
          <label className="text-xs font-medium text-text-dim">Profile</label>
          <select
            value={profile}
            onChange={(e) => setProfile(e.target.value)}
            className="mt-1 w-full rounded-md border border-border bg-surface-2 px-3 py-2 text-sm"
          >
            {profiles.map((p) => (
              <option key={p} value={p}>
                {p}
              </option>
            ))}
          </select>
        </div>
      </div>

      <div>
        <label className="text-xs font-medium text-text-dim">Build engine</label>
        <div className="mt-1 grid grid-cols-3 gap-2">
          {(["auto", "gradle", "eas"] as Engine[]).map((e) => (
            <button
              type="button"
              key={e}
              onClick={() => setEngine(e)}
              className={`rounded-md border px-2 py-2 font-mono text-xs transition-colors ${
                engine === e ? "border-accent bg-accent-soft text-accent" : "border-border text-text-dim hover:text-text"
              }`}
            >
              {e === "auto" ? "Auto" : e === "gradle" ? "Gradle (local)" : "EAS (local)"}
            </button>
          ))}
        </div>
        <p className="mt-1 font-mono text-[11px] text-text-dim">
          Auto uses EAS if an Expo token is supplied below, otherwise builds fully offline with Gradle.
        </p>
      </div>

      {engine !== "gradle" && (
        <div>
          <label className="text-xs font-medium text-text-dim">Expo access token (optional)</label>
          <input
            type="password"
            value={expoToken}
            onChange={(e) => setExpoToken(e.target.value)}
            placeholder="Only needed for the EAS engine"
            className="mt-1 w-full rounded-md border border-border bg-surface-2 px-3 py-2 text-sm"
          />
        </div>
      )}

      <div>
        <label className="text-xs font-medium text-text-dim">Signing</label>
        <div className="mt-1 grid grid-cols-2 gap-2">
          {(["debug", "release"] as SigningMode[]).map((m) => (
            <button
              type="button"
              key={m}
              onClick={() => setSigningMode(m)}
              className={`rounded-md border px-2 py-2 font-mono text-xs transition-colors ${
                signingMode === m ? "border-accent bg-accent-soft text-accent" : "border-border text-text-dim hover:text-text"
              }`}
            >
              {m === "debug" ? "Debug" : "Release"}
            </button>
          ))}
        </div>
      </div>

      {signingMode === "release" && <KeystoreManager value={keystoreId} onChange={setKeystoreId} />}

      {error && <p className="font-mono text-xs text-danger">{error}</p>}

      <button
        type="submit"
        disabled={submitting}
        className="w-full rounded-md bg-accent px-4 py-2.5 font-mono text-sm font-medium text-[#1a1006] transition-opacity hover:opacity-90 disabled:opacity-50"
      >
        {submitting ? "Starting build…" : `Build ${project.name ?? "app"}`}
      </button>
    </form>
  );
}
