import crypto from 'node:crypto';
import type { FastifyInstance } from 'fastify';
import * as db from '../store/db';
import { encrypt } from '../util/crypto';

/**
 * Saved Expo access tokens, one per EAS account ("owner" — app.json's `expo.owner`
 * slug), so `ebl build`/the GUI can auto-select the right token for a project
 * instead of the operator pasting one in per build. `owner: ""` is the default/
 * fallback entry (see build/manager.ts's resolveExpoToken). Tokens are AES-256-GCM-
 * encrypted at rest (see util/crypto.ts) — decrypted only in memory, immediately
 * before being injected into a runner container's environment. The list endpoint
 * never returns the token itself.
 */
export default async function expoTokenRoutes(app: FastifyInstance): Promise<void> {
  app.get('/api/expo-tokens', async () => ({ expoTokens: db.listExpoTokens() }));

  app.post<{ Body: { owner?: string; token: string; label?: string } }>(
    '/api/expo-tokens',
    async (request, reply) => {
      const { owner, token, label } = request.body ?? ({} as { owner?: string; token: string; label?: string });
      if (!token) return reply.code(400).send({ error: 'token is required' });

      const existing = db.getExpoTokenSecretByOwner(owner ?? '');
      const id = existing?.id ?? crypto.randomUUID();
      const createdAt = existing?.createdAt ?? Date.now();
      db.upsertExpoToken({
        id,
        owner: owner ?? '',
        label: label ?? null,
        tokenEnc: encrypt(token),
        createdAt,
      });

      return reply.code(201).send({ id, owner: owner ?? '', label: label ?? null, createdAt });
    }
  );

  app.delete<{ Params: { id: string } }>('/api/expo-tokens/:id', async (request, reply) => {
    db.deleteExpoToken(request.params.id);
    return reply.code(204).send();
  });
}
