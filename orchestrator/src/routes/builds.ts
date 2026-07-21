import fs from 'node:fs';
import path from 'node:path';
import type { FastifyInstance } from 'fastify';
import * as db from '../store/db';
import { startBuild, cancelBuild, readLogFile, ValidationError } from '../build/manager';
import type { StartBuildRequest } from '../types';

export default async function buildRoutes(app: FastifyInstance): Promise<void> {
  app.post<{ Body: StartBuildRequest }>('/api/builds', async (request, reply) => {
    try {
      const build = startBuild(request.body);
      return reply.code(201).send({ build });
    } catch (err) {
      if (err instanceof ValidationError) {
        return reply.code(400).send({ error: err.message });
      }
      request.log.error(err);
      return reply.code(500).send({ error: 'Failed to start build' });
    }
  });

  app.get('/api/builds', async (request) => {
    const limit = Number((request.query as any)?.limit ?? 100);
    return { builds: db.listBuilds(limit) };
  });

  app.get<{ Params: { id: string } }>('/api/builds/:id', async (request, reply) => {
    const build = db.getBuild(request.params.id);
    if (!build) return reply.code(404).send({ error: 'Build not found' });
    return { build, phases: db.listPhases(build.id) };
  });

  app.get<{ Params: { id: string } }>('/api/builds/:id/stats', async (request, reply) => {
    const build = db.getBuild(request.params.id);
    if (!build) return reply.code(404).send({ error: 'Build not found' });
    return { samples: db.listStatSamples(build.id) };
  });

  app.get<{ Params: { id: string } }>('/api/builds/:id/log', async (request, reply) => {
    const build = db.getBuild(request.params.id);
    if (!build) return reply.code(404).send({ error: 'Build not found' });
    reply.type('text/plain; charset=utf-8');
    return readLogFile(build.id);
  });

  app.get<{ Params: { id: string } }>('/api/builds/:id/artifact', async (request, reply) => {
    const build = db.getBuild(request.params.id);
    if (!build || !build.artifactPath) {
      return reply.code(404).send({ error: 'No artifact available for this build' });
    }
    if (!fs.existsSync(build.artifactPath)) {
      return reply.code(410).send({ error: 'Artifact file no longer exists on disk' });
    }
    reply.header('Content-Disposition', `attachment; filename="${path.basename(build.artifactPath)}"`);
    reply.type('application/octet-stream');
    return reply.send(fs.createReadStream(build.artifactPath));
  });

  app.post<{ Params: { id: string } }>('/api/builds/:id/cancel', async (request, reply) => {
    const ok = cancelBuild(request.params.id);
    if (!ok) return reply.code(409).send({ error: 'Build is not cancellable in its current state' });
    return { cancelled: true };
  });
}
