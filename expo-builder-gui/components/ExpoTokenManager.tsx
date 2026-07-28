"use client";

import { useEffect, useState } from "react";
import { deleteExpoToken, listExpoTokens, saveExpoToken } from "@/lib/api";
import type { ExpoTokenRecord } from "@/lib/types";

/** Manages saved per-account Expo tokens (keyed by app.json's `expo.owner`), used to
 * auto-select the right token for a project instead of pasting one in per build.
 * `detectedOwner` (if any) is highlighted so it's clear which entry (if any) a build
 * of the currently-selected project would actually use. */
export default function ExpoTokenManager({ detectedOwner }: { detectedOwner?: string }) {
  const [tokens, setTokens] = useState<ExpoTokenRecord[]>([]);
  const [showAdd, setShowAdd] = useState(false);
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState<string | null>(null);

  function refresh() {
    listExpoTokens()
      .then((r) => setTokens(r.expoTokens))
      .catch((err) => setError(err.message));
  }

  useEffect(() => {
    refresh();
  }, []);

  async function handleSave(formEvent: React.FormEvent<HTMLFormElement>) {
    formEvent.preventDefault();
    setError(null);
    setSaving(true);
    try {
      const form = new FormData(formEvent.currentTarget);
      await saveExpoToken({
        owner: String(form.get("owner") ?? "").trim(),
        token: String(form.get("token") ?? ""),
        label: String(form.get("label") ?? "") || undefined,
      });
      formEvent.currentTarget.reset();
      setShowAdd(false);
      refresh();
    } catch (err: any) {
      setError(err.message);
    } finally {
      setSaving(false);
    }
  }

  async function handleDelete(id: string) {
    await deleteExpoToken(id);
    refresh();
  }

  const matched = detectedOwner !== undefined
    ? tokens.find((t) => t.owner === detectedOwner) ?? tokens.find((t) => t.owner === "")
    : undefined;

  return (
    <div className="space-y-2 rounded-md border border-border p-3">
      <div className="flex items-center justify-between">
        <label className="text-xs font-medium text-text-dim">Saved Expo tokens (by account)</label>
        {!showAdd && (
          <button type="button" onClick={() => setShowAdd(true)} className="font-mono text-xs text-accent hover:underline">
            + add
          </button>
        )}
      </div>

      {detectedOwner !== undefined && (
        <p className="font-mono text-[11px] text-text-dim">
          This project&apos;s owner: <span className="text-text">{detectedOwner || "(none in app.json)"}</span> —{" "}
          {matched ? (
            <span className="text-accent">
              will use the saved &quot;{matched.owner || "default"}&quot; token automatically
            </span>
          ) : (
            <span>no saved token matches yet</span>
          )}
        </p>
      )}

      {tokens.length > 0 && (
        <ul className="space-y-1">
          {tokens.map((t) => (
            <li key={t.id} className="flex items-center justify-between font-mono text-xs text-text-dim">
              <span className="truncate">
                {t.owner || "(default)"}
                {t.label ? ` · ${t.label}` : ""}
              </span>
              <button type="button" onClick={() => handleDelete(t.id)} className="text-danger hover:underline">
                remove
              </button>
            </li>
          ))}
        </ul>
      )}

      {showAdd && (
        <form onSubmit={handleSave} className="space-y-2 rounded-md border border-border p-3">
          <input
            name="owner"
            placeholder="Owner (app.json's expo.owner, e.g. project-cell — blank = default)"
            className="w-full rounded-md border border-border bg-surface-2 px-2 py-1.5 text-xs"
          />
          <input
            name="token"
            type="password"
            placeholder="Expo access token"
            required
            className="w-full rounded-md border border-border bg-surface-2 px-2 py-1.5 text-xs"
          />
          <input
            name="label"
            placeholder="Label (optional, e.g. personal account)"
            className="w-full rounded-md border border-border bg-surface-2 px-2 py-1.5 text-xs"
          />
          <div className="flex gap-2">
            <button
              type="submit"
              disabled={saving}
              className="flex-1 rounded-md bg-accent px-3 py-1.5 font-mono text-xs font-medium text-[#1a1006] disabled:opacity-50"
            >
              {saving ? "Saving…" : "Save token"}
            </button>
            <button type="button" onClick={() => setShowAdd(false)} className="rounded-md border border-border px-3 py-1.5 font-mono text-xs">
              Cancel
            </button>
          </div>
        </form>
      )}

      {error && <p className="font-mono text-xs text-danger">{error}</p>}
      <p className="font-mono text-[11px] text-text-dim">
        Tokens are encrypted at rest and only ever decrypted in memory for the build that uses them. Saving a
        token for an owner that already has one replaces it.
      </p>
    </div>
  );
}
