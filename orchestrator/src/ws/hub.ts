import type { WebSocket } from 'ws';
import type { BuildWsMessage } from '../types';

/**
 * Tiny per-build pub/sub registry. `routes/builds.ts` upgrades `/ws/builds/:id`
 * connections into this hub; `build/manager.ts` publishes log/phase/progress/stats
 * events as they happen. A build can have zero, one, or several viewers (e.g. two
 * browser tabs) — all get the same live stream.
 */
class BuildHub {
  private subscribers = new Map<string, Set<WebSocket>>();

  subscribe(buildId: string, socket: WebSocket): void {
    let set = this.subscribers.get(buildId);
    if (!set) {
      set = new Set();
      this.subscribers.set(buildId, set);
    }
    set.add(socket);
    socket.on('close', () => {
      set?.delete(socket);
      if (set && set.size === 0) this.subscribers.delete(buildId);
    });
  }

  publish(buildId: string, message: BuildWsMessage): void {
    const set = this.subscribers.get(buildId);
    if (!set || set.size === 0) return;
    const payload = JSON.stringify(message);
    for (const socket of set) {
      if (socket.readyState === socket.OPEN) socket.send(payload);
    }
  }
}

export const buildHub = new BuildHub();
