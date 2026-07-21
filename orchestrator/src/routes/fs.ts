import fs from 'node:fs';
import path from 'node:path';
import type { FastifyInstance } from 'fastify';
import { config } from '../config';
import { detectExpoProject, isPathAllowed } from '../build/detect';
import type { DirEntry } from '../types';

export default async function fsRoutes(app: FastifyInstance): Promise<void> {
  // The roots the picker is allowed to start browsing from (configured via
  // ALLOWED_ROOTS, bind-mounted into the orchestrator container at the same paths).
  app.get('/api/fs/roots', async () => ({ roots: config.allowedRoots }));

  app.get<{ Querystring: { path?: string } }>('/api/fs/list', async (request, reply) => {
    const target = path.resolve(request.query.path ?? config.allowedRoots[0]);
    if (!isPathAllowed(target)) {
      return reply.code(403).send({ error: `Path is outside the allowed roots: ${target}` });
    }
    if (!fs.existsSync(target) || !fs.statSync(target).isDirectory()) {
      return reply.code(404).send({ error: 'Directory not found' });
    }

    let names: string[];
    try {
      names = fs.readdirSync(target);
    } catch (err: any) {
      return reply.code(500).send({ error: `Cannot read directory: ${err?.message ?? err}` });
    }

    const entries: DirEntry[] = names
      .filter((name) => !name.startsWith('.'))
      .map((name) => {
        const full = path.join(target, name);
        let isDirectory = false;
        try {
          isDirectory = fs.statSync(full).isDirectory();
        } catch {
          isDirectory = false;
        }
        return { name, path: full, isDirectory };
      })
      .filter((e) => e.isDirectory)
      .sort((a, b) => a.name.localeCompare(b.name));

    const parent = path.dirname(target);
    const project = detectExpoProject(target);

    return {
      path: target,
      parent: isPathAllowed(parent) && parent !== target ? parent : null,
      entries,
      project,
    };
  });
}
