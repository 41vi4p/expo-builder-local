"use client";

import { useEffect, useState } from "react";
import { deleteKeystore, listKeystores, uploadKeystore } from "@/lib/api";
import type { KeystoreRecord } from "@/lib/types";

export default function KeystoreManager({
  value,
  onChange,
}: {
  value: string | null;
  onChange: (id: string | null) => void;
}) {
  const [keystores, setKeystores] = useState<KeystoreRecord[]>([]);
  const [showUpload, setShowUpload] = useState(false);
  const [uploading, setUploading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  function refresh() {
    listKeystores()
      .then((r) => setKeystores(r.keystores))
      .catch((err) => setError(err.message));
  }

  useEffect(() => {
    refresh();
  }, []);

  async function handleUpload(formEvent: React.FormEvent<HTMLFormElement>) {
    formEvent.preventDefault();
    setError(null);
    setUploading(true);
    try {
      const form = new FormData(formEvent.currentTarget);
      const created = await uploadKeystore(form);
      formEvent.currentTarget.reset();
      setShowUpload(false);
      refresh();
      onChange(created.id);
    } catch (err: any) {
      setError(err.message);
    } finally {
      setUploading(false);
    }
  }

  async function handleDelete(id: string) {
    if (value === id) onChange(null);
    await deleteKeystore(id);
    refresh();
  }

  return (
    <div className="space-y-2">
      <label className="text-xs font-medium text-text-dim">Release keystore</label>
      <select
        value={value ?? ""}
        onChange={(e) => onChange(e.target.value || null)}
        className="w-full rounded-md border border-border bg-surface-2 px-3 py-2 text-sm"
      >
        <option value="">Debug keystore (quick install, not for the Play Store)</option>
        {keystores.map((k) => (
          <option key={k.id} value={k.id}>
            {k.name} ({k.keyAlias})
          </option>
        ))}
      </select>

      {keystores.length > 0 && (
        <ul className="space-y-1">
          {keystores.map((k) => (
            <li key={k.id} className="flex items-center justify-between font-mono text-xs text-text-dim">
              <span className="truncate">
                {k.name} · {k.keyAlias}
              </span>
              <button type="button" onClick={() => handleDelete(k.id)} className="text-danger hover:underline">
                remove
              </button>
            </li>
          ))}
        </ul>
      )}

      {!showUpload ? (
        <button
          type="button"
          onClick={() => setShowUpload(true)}
          className="font-mono text-xs text-accent hover:underline"
        >
          + upload a keystore
        </button>
      ) : (
        <form onSubmit={handleUpload} className="space-y-2 rounded-md border border-border p-3">
          <input
            name="file"
            type="file"
            accept=".jks,.keystore"
            required
            className="w-full text-xs file:mr-2 file:rounded file:border-0 file:bg-surface-2 file:px-2 file:py-1 file:text-xs"
          />
          <input name="name" placeholder="Label (e.g. play-store-release)" required className="w-full rounded-md border border-border bg-surface-2 px-2 py-1.5 text-xs" />
          <input name="keyAlias" placeholder="Key alias" required className="w-full rounded-md border border-border bg-surface-2 px-2 py-1.5 text-xs" />
          <input name="storePassword" type="password" placeholder="Store password" required className="w-full rounded-md border border-border bg-surface-2 px-2 py-1.5 text-xs" />
          <input name="keyPassword" type="password" placeholder="Key password (defaults to store password)" className="w-full rounded-md border border-border bg-surface-2 px-2 py-1.5 text-xs" />
          <div className="flex gap-2">
            <button
              type="submit"
              disabled={uploading}
              className="flex-1 rounded-md bg-accent px-3 py-1.5 font-mono text-xs font-medium text-[#1a1006] disabled:opacity-50"
            >
              {uploading ? "Uploading…" : "Save keystore"}
            </button>
            <button type="button" onClick={() => setShowUpload(false)} className="rounded-md border border-border px-3 py-1.5 font-mono text-xs">
              Cancel
            </button>
          </div>
        </form>
      )}

      {error && <p className="font-mono text-xs text-danger">{error}</p>}
      <p className="font-mono text-[11px] text-text-dim">
        Passwords are encrypted at rest and only ever decrypted in memory for the build that uses them.
      </p>
    </div>
  );
}
