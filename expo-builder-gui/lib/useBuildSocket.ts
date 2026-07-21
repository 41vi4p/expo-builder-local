"use client";

import { useEffect, useRef, useState } from "react";
import { wsUrl } from "./api";
import type { BuildPhase, BuildRecord, BuildStatus, BuildWsMessage, StatSample } from "./types";

export interface BuildSocketState {
  build: BuildRecord | null;
  phases: BuildPhase[];
  log: string;
  stats: StatSample[];
  connected: boolean;
}

const TERMINAL_STATUSES: BuildStatus[] = ["success", "failed", "cancelled"];

/** Subscribes to a build's live WebSocket stream and keeps a rolling view of its
 * state (metadata, phase history, full log text, resource-usage samples). Reconnects
 * with backoff on drop, and stops trying once the build reaches a terminal status. */
export function useBuildSocket(buildId: string): BuildSocketState {
  const [state, setState] = useState<BuildSocketState>({
    build: null,
    phases: [],
    log: "",
    stats: [],
    connected: false,
  });
  const stoppedRef = useRef(false);
  const statusRef = useRef<BuildStatus | null>(null);

  useEffect(() => {
    stoppedRef.current = false;
    let socket: WebSocket | null = null;
    let retryDelay = 1000;
    let retryTimer: ReturnType<typeof setTimeout> | null = null;

    function connect() {
      if (stoppedRef.current) return;
      socket = new WebSocket(wsUrl(`/ws/builds/${buildId}`));

      socket.onopen = () => {
        retryDelay = 1000;
        setState((s) => ({ ...s, connected: true }));
      };

      socket.onmessage = (event) => {
        const msg: BuildWsMessage = JSON.parse(event.data);
        if (msg.type === "snapshot") statusRef.current = msg.build.status;
        if (msg.type === "status") statusRef.current = msg.build?.status ?? msg.status;
        setState((s) => {
          switch (msg.type) {
            case "snapshot":
              return { ...s, build: msg.build, phases: msg.phases, log: msg.log, stats: msg.stats };
            case "log":
              return { ...s, log: s.log + msg.line };
            case "phase":
              return {
                ...s,
                phases: [...s.phases, { phase: msg.phase, label: msg.label, startedAt: Date.now(), endedAt: null }],
                build: s.build && { ...s.build, currentPhase: msg.phase, currentPhaseLabel: msg.label },
              };
            case "progress":
              return {
                ...s,
                build: s.build && { ...s.build, progress: msg.percent, etaSeconds: msg.etaSeconds },
              };
            case "stats":
              return { ...s, stats: [...s.stats, msg.sample].slice(-600) };
            case "status":
              return {
                ...s,
                build: msg.build ?? (s.build && { ...s.build, status: msg.status }),
              };
            case "error":
              return { ...s, build: s.build && { ...s.build, error: msg.message } };
            default:
              return s;
          }
        });
      };

      socket.onclose = () => {
        setState((s) => ({ ...s, connected: false }));
        if (stoppedRef.current) return;
        if (statusRef.current && TERMINAL_STATUSES.includes(statusRef.current)) return;
        retryTimer = setTimeout(connect, retryDelay);
        retryDelay = Math.min(retryDelay * 2, 15000);
      };

      socket.onerror = () => {
        socket?.close();
      };
    }

    connect();
    return () => {
      stoppedRef.current = true;
      if (retryTimer) clearTimeout(retryTimer);
      socket?.close();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [buildId]);

  return state;
}
