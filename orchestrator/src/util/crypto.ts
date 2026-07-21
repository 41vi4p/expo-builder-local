import crypto from 'node:crypto';
import { config } from '../config';

/**
 * AES-256-GCM at-rest encryption for keystore passwords. The master key is provided
 * once via MASTER_KEY (base64 or hex, 32 bytes) and never stored alongside the data.
 * Encrypted values are only ever decrypted in-memory, immediately before being passed
 * as an env var into a short-lived runner container — the API never returns them.
 */

function resolveKey(): Buffer {
  const raw = config.masterKey;
  let key: Buffer;
  if (/^[0-9a-fA-F]{64}$/.test(raw)) {
    key = Buffer.from(raw, 'hex');
  } else {
    key = Buffer.from(raw, 'base64');
  }
  if (key.length !== 32) {
    throw new Error(
      `MASTER_KEY must decode to 32 bytes (got ${key.length}). Generate one with: openssl rand -base64 32`
    );
  }
  return key;
}

const KEY = resolveKey();

export function encrypt(plaintext: string): string {
  const iv = crypto.randomBytes(12);
  const cipher = crypto.createCipheriv('aes-256-gcm', KEY, iv);
  const enc = Buffer.concat([cipher.update(plaintext, 'utf8'), cipher.final()]);
  const tag = cipher.getAuthTag();
  return Buffer.concat([iv, tag, enc]).toString('base64');
}

export function decrypt(payload: string): string {
  const buf = Buffer.from(payload, 'base64');
  const iv = buf.subarray(0, 12);
  const tag = buf.subarray(12, 28);
  const enc = buf.subarray(28);
  const decipher = crypto.createDecipheriv('aes-256-gcm', KEY, iv);
  decipher.setAuthTag(tag);
  return Buffer.concat([decipher.update(enc), decipher.final()]).toString('utf8');
}
