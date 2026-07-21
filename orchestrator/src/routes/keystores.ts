import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import type { FastifyInstance } from 'fastify';
import { config } from '../config';
import * as db from '../store/db';
import { encrypt } from '../util/crypto';

/**
 * Keystore uploads. The .jks/.keystore file itself is stored as-is on disk (Gradle/EAS
 * both need a real file path); the store/key passwords are the only sensitive fields
 * and are AES-256-GCM-encrypted at rest (see util/crypto.ts) — decrypted only in
 * memory, immediately before being injected into a runner container's environment for
 * that one build. The list endpoint never returns password fields, encrypted or not.
 */
export default async function keystoreRoutes(app: FastifyInstance): Promise<void> {
  app.get('/api/keystores', async () => ({ keystores: db.listKeystores() }));

  app.post('/api/keystores', async (request, reply) => {
    const parts = request.parts();
    let fileBuffer: Buffer | null = null;
    let originalFilename = '';
    const fields: Record<string, string> = {};

    for await (const part of parts) {
      if (part.type === 'file') {
        const chunks: Buffer[] = [];
        for await (const chunk of part.file) chunks.push(chunk as Buffer);
        fileBuffer = Buffer.concat(chunks);
        originalFilename = part.filename;
      } else {
        fields[part.fieldname] = String(part.value);
      }
    }

    if (!fileBuffer) return reply.code(400).send({ error: 'Missing keystore file' });
    const { name, storePassword, keyAlias, keyPassword } = fields;
    if (!name || !storePassword || !keyAlias) {
      return reply.code(400).send({ error: 'name, storePassword and keyAlias are required' });
    }

    const id = crypto.randomUUID();
    const ext = path.extname(originalFilename) || '.jks';
    const filename = `${id}${ext}`;
    const storagePath = path.join(config.keystoreDir, filename);
    fs.writeFileSync(storagePath, fileBuffer, { mode: 0o600 });

    db.insertKeystore({
      id,
      name,
      filename,
      storagePath,
      keyAlias,
      storePasswordEnc: encrypt(storePassword),
      keyPasswordEnc: encrypt(keyPassword || storePassword),
      createdAt: Date.now(),
    });

    return reply.code(201).send({ id, name, filename, keyAlias, createdAt: Date.now() });
  });

  app.delete<{ Params: { id: string } }>('/api/keystores/:id', async (request, reply) => {
    const secret = db.getKeystoreSecret(request.params.id);
    if (!secret) return reply.code(404).send({ error: 'Keystore not found' });
    fs.rmSync(secret.storagePath, { force: true });
    db.deleteKeystore(request.params.id);
    return reply.code(204).send();
  });
}
