import { medianDurationSeconds } from '../store/db';
import type { ResolvedEngine } from '../types';

/**
 * Turns the runner's raw stdout stream into {phase, percent, etaSeconds} updates for
 * the live dashboard.
 *
 * Two sources feed this:
 *  1. The `@@PHASE:<id>:<label>` / `@@PROGRESS:<0-100>` markers build-entrypoint.sh
 *     emits at coarse phase boundaries (setup, install, prebuild, signing, collect).
 *  2. Gradle's own live `<====> NN% EXECUTING` line, available because the runner
 *     container is allocated a TTY and Gradle is invoked with `--console=rich` — this
 *     gives much finer-grained progress during the expensive gradle phase than any
 *     marker we could emit ourselves.
 *
 * Percent is tracked as a single monotonically-increasing 0-100 value blended from
 * fixed phase weights (below) plus fractional progress *within* the current phase.
 */

type PhaseId = 'setup' | 'install' | 'prebuild' | 'signing' | 'gradle' | 'eas' | 'collect' | 'done';

const GRADLE_WEIGHTS: Record<PhaseId, number> = {
  setup: 2,
  install: 15,
  prebuild: 10,
  signing: 3,
  gradle: 65,
  eas: 0,
  collect: 5,
  done: 0,
};

const EAS_WEIGHTS: Record<PhaseId, number> = {
  setup: 2,
  install: 15,
  prebuild: 0,
  signing: 3,
  gradle: 0,
  eas: 75,
  collect: 5,
  done: 0,
};

const PHASE_ORDER: PhaseId[] = ['setup', 'install', 'prebuild', 'signing', 'gradle', 'eas', 'collect', 'done'];

const GRADLE_PROGRESS_RE = /(\d{1,3})%\s+(EXECUTING|CONFIGURING|INITIALIZING)/;

// Coarse, best-effort milestones within `eas build --local` output — the CLI doesn't
// print a live percentage, so we step through recognizable phase-transition lines.
const EAS_MILESTONES: Array<{ pattern: RegExp; fraction: number }> = [
  { pattern: /Resolving credentials/i, fraction: 0.1 },
  { pattern: /Compressing project/i, fraction: 0.2 },
  { pattern: /Prebuild|Running.*prebuild/i, fraction: 0.35 },
  { pattern: /Installing/i, fraction: 0.45 },
  { pattern: /Running gradle|Gradle build/i, fraction: 0.55 },
  { pattern: /BUILD SUCCESSFUL/i, fraction: 0.95 },
];

export type ProgressEvent =
  | { type: 'phase'; phase: string; label: string }
  | { type: 'progress'; percent: number; etaSeconds: number | null };

export class ProgressTracker {
  private engine: ResolvedEngine | 'gradle' = 'gradle';
  private weights = GRADLE_WEIGHTS;
  private phaseIndex = 0;
  private percent = 0;
  private readonly startedAt = Date.now();
  private readonly medianDurationMs: number | null;
  private easMilestoneIdx = 0;

  constructor(private readonly appPath: string, private readonly profile: string) {
    const median = medianDurationSeconds(appPath, profile);
    this.medianDurationMs = median ? median * 1000 : null;
  }

  private baseForPhase(id: PhaseId): number {
    let base = 0;
    for (const p of PHASE_ORDER) {
      if (p === id) break;
      base += this.weights[p];
    }
    return base;
  }

  private clampAdvance(pct: number): number {
    this.percent = Math.max(this.percent, Math.min(100, pct));
    return this.percent;
  }

  private eta(): number | null {
    const elapsedMs = Date.now() - this.startedAt;
    if (this.percent <= 1) return this.medianDurationMs ? Math.round(this.medianDurationMs / 1000) : null;
    const extrapolatedTotalMs = (elapsedMs / this.percent) * 100;
    const remainingFromExtrapolation = extrapolatedTotalMs - elapsedMs;
    if (!this.medianDurationMs) return Math.max(0, Math.round(remainingFromExtrapolation / 1000));
    // Blend live extrapolation with historical median (60/40) so ETA doesn't swing
    // wildly in the first few seconds of a phase before enough signal has arrived.
    const remainingFromMedian = Math.max(0, this.medianDurationMs - elapsedMs);
    const blended = remainingFromExtrapolation * 0.6 + remainingFromMedian * 0.4;
    return Math.max(0, Math.round(blended / 1000));
  }

  /** Call once the runner resolves auto → eas|gradle (the `@@ENGINE:` marker). */
  setEngine(engine: ResolvedEngine): void {
    this.engine = engine;
    this.weights = engine === 'eas' ? EAS_WEIGHTS : GRADLE_WEIGHTS;
  }

  /** Parses one line of runner stdout/stderr, returning any dashboard events it produced. */
  handleLine(line: string): ProgressEvent[] {
    const events: ProgressEvent[] = [];

    const phaseMatch = line.match(/^@@PHASE:([a-z]+):(.*)$/);
    if (phaseMatch) {
      const [, id, label] = phaseMatch;
      this.phaseIndex = PHASE_ORDER.indexOf(id as PhaseId);
      const base = this.baseForPhase(id as PhaseId);
      events.push({ type: 'phase', phase: id, label });
      events.push({ type: 'progress', percent: this.clampAdvance(base), etaSeconds: this.eta() });
      return events;
    }

    const progressMatch = line.match(/^@@PROGRESS:(\d{1,3})$/);
    if (progressMatch) {
      const currentPhase = PHASE_ORDER[this.phaseIndex];
      const base = this.baseForPhase(currentPhase);
      const budget = this.weights[currentPhase];
      const withinPhasePct = Number(progressMatch[1]) / 100;
      events.push({ type: 'progress', percent: this.clampAdvance(base + budget * withinPhasePct), etaSeconds: this.eta() });
      return events;
    }

    if (this.engine === 'gradle' && PHASE_ORDER[this.phaseIndex] === 'gradle') {
      const gradleMatch = line.match(GRADLE_PROGRESS_RE);
      if (gradleMatch) {
        const base = this.baseForPhase('gradle');
        const budget = this.weights.gradle;
        const pct = Number(gradleMatch[1]) / 100;
        events.push({ type: 'progress', percent: this.clampAdvance(base + budget * pct), etaSeconds: this.eta() });
        return events;
      }
    }

    if (this.engine === 'eas' && PHASE_ORDER[this.phaseIndex] === 'eas') {
      for (let i = this.easMilestoneIdx; i < EAS_MILESTONES.length; i++) {
        if (EAS_MILESTONES[i].pattern.test(line)) {
          this.easMilestoneIdx = i + 1;
          const base = this.baseForPhase('eas');
          const budget = this.weights.eas;
          events.push({
            type: 'progress',
            percent: this.clampAdvance(base + budget * EAS_MILESTONES[i].fraction),
            etaSeconds: this.eta(),
          });
          break;
        }
      }
    }

    return events;
  }

  finish(): ProgressEvent {
    this.percent = 100;
    return { type: 'progress', percent: 100, etaSeconds: 0 };
  }
}
