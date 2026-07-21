import 'dotenv/config';
import Fastify from 'fastify';
import cors from '@fastify/cors';
import multipart from '@fastify/multipart';
import { WebSocketServer } from 'ws';
import { config } from './config';
import fsRoutes from './routes/fs';
import buildRoutes from './routes/builds';
import keystoreRoutes from './routes/keystores';
import { buildHub } from './ws/hub';
import * as db from './store/db';
import { readLogFile } from './build/manager';
import type { BuildWsMessage } from './types';

async function main() {
  const app = Fastify({ logger: true, bodyLimit: 1024 * 1024 });

  await app.register(cors, { origin: config.corsOrigin });
  await app.register(multipart, {
    limits: { fileSize: 64 * 1024 * 1024 }, // keystores are tiny; 64MB is generous headroom
  });

  app.get('/api/health', async () => ({ ok: true }));

  await app.register(fsRoutes);
  await app.register(buildRoutes);
  await app.register(keystoreRoutes);

  await app.ready();

  // Live build dashboard socket: GET /ws/builds/:id. Handled as a raw `ws` upgrade on
  // Fastify's underlying HTTP server rather than a plugin, so we have direct control
  // over per-build subscriber routing (see ws/hub.ts).
  const wss = new WebSocketServer({ noServer: true });
  app.server.on('upgrade', (request, socket, head) => {
    const url = new URL(request.url ?? '', 'http://internal');
    const match = url.pathname.match(/^\/ws\/builds\/([^/]+)$/);
    if (!match) {
      socket.destroy();
      return;
    }
    const buildId = match[1];
    wss.handleUpgrade(request, socket, head, (ws) => {
      const build = db.getBuild(buildId);
      if (!build) {
        ws.close(4404, 'Build not found');
        return;
      }
      const snapshot: BuildWsMessage = {
        type: 'snapshot',
        build,
        phases: db.listPhases(buildId),
        log: readLogFile(buildId),
        stats: db.listStatSamples(buildId),
      };
      ws.send(JSON.stringify(snapshot));
      buildHub.subscribe(buildId, ws);
    });
  });

  await app.listen({ port: config.port, host: config.host });
  app.log.info(`expo-builder-local orchestrator listening on ${config.host}:${config.port}`);
  app.log.info(`Allowed browse roots: ${config.allowedRoots.join(', ')}`);
}

main().catch((err) => {
  console.error('Fatal startup error:', err);
  process.exit(1);
});
