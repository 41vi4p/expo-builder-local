"use client";

import { useMemo, useRef, useState, useEffect } from "react";
import { Canvas, useFrame } from "@react-three/fiber";
import { Float, RoundedBox } from "@react-three/drei";
import * as THREE from "three";

// Six blocks — one per real build phase from build-entrypoint.sh's own marker
// protocol (setup/install/prebuild-or-eas/signing/compile/collect). Not labeled in
// 3D (hard to read on a phone, bad for accessibility) — the "How it works" section
// below spells them out in real DOM text. This scene is the atmosphere: a build
// pipeline rendered as drifting, orbiting geometry with one warm light standing in
// for the live progress indicator every build shows.
const PHASES = [
  { pos: [-2.6, 0.6, -0.5], color: "#e8944a", scale: 0.62 },
  { pos: [-1.5, -0.5, 0.4], color: "#4fc3d9", scale: 0.5 },
  { pos: [-0.3, 0.85, 0.1], color: "#e8944a", scale: 0.58 },
  { pos: [1.0, -0.35, -0.3], color: "#e8944a", scale: 0.66 },
  { pos: [2.2, 0.5, 0.3], color: "#4fc3d9", scale: 0.48 },
  { pos: [3.2, -0.6, -0.2], color: "#4ade80", scale: 0.5 },
] as const;

function usePrefersReducedMotion() {
  const [reduced, setReduced] = useState(false);
  useEffect(() => {
    const mq = window.matchMedia("(prefers-reduced-motion: reduce)");
    setReduced(mq.matches);
    const onChange = () => setReduced(mq.matches);
    mq.addEventListener("change", onChange);
    return () => mq.removeEventListener("change", onChange);
  }, []);
  return reduced;
}

function Block({
  position,
  color,
  scale,
  reduced,
  index,
}: {
  position: readonly [number, number, number];
  color: string;
  scale: number;
  reduced: boolean;
  index: number;
}) {
  const mesh = useRef<THREE.Mesh>(null);
  useFrame((_, delta) => {
    if (reduced || !mesh.current) return;
    mesh.current.rotation.x += delta * 0.12;
    mesh.current.rotation.y += delta * 0.18;
  });

  const body = (
    <RoundedBox ref={mesh} args={[1, 1, 1]} radius={0.16} smoothness={4} scale={scale}>
      <meshStandardMaterial color={color} roughness={0.35} metalness={0.2} />
    </RoundedBox>
  );

  if (reduced) {
    return <group position={position as unknown as [number, number, number]}>{body}</group>;
  }

  return (
    <Float
      position={position as unknown as [number, number, number]}
      speed={1.1 + index * 0.15}
      rotationIntensity={0.3}
      floatIntensity={0.9}
    >
      {body}
    </Float>
  );
}

function OrbitingLight({ reduced }: { reduced: boolean }) {
  const light = useRef<THREE.PointLight>(null);
  useFrame(({ clock }) => {
    if (reduced || !light.current) return;
    const t = clock.getElapsedTime() * 0.25;
    light.current.position.set(Math.cos(t) * 4, Math.sin(t * 1.3) * 1.4, Math.sin(t) * 2 + 1.5);
  });
  return <pointLight ref={light} position={[2, 1, 2]} color="#ffb066" intensity={22} distance={9} />;
}

function Scene({ reduced }: { reduced: boolean }) {
  const group = useRef<THREE.Group>(null);
  const target = useRef({ x: 0, y: 0 });

  useEffect(() => {
    if (reduced) return;
    const onMove = (e: PointerEvent) => {
      target.current.x = (e.clientX / window.innerWidth - 0.5) * 0.35;
      target.current.y = (e.clientY / window.innerHeight - 0.5) * 0.2;
    };
    window.addEventListener("pointermove", onMove);
    return () => window.removeEventListener("pointermove", onMove);
  }, [reduced]);

  useFrame((_, delta) => {
    if (!group.current) return;
    if (!reduced) {
      group.current.rotation.y += delta * 0.045;
    }
    group.current.rotation.x = THREE.MathUtils.damp(group.current.rotation.x, target.current.y, 4, delta);
    group.current.position.y = THREE.MathUtils.damp(group.current.position.y, -target.current.y * 0.4, 4, delta);
  });

  return (
    <group ref={group}>
      {PHASES.map((p, i) => (
        <Block key={i} position={p.pos} color={p.color} scale={p.scale} reduced={reduced} index={i} />
      ))}
    </group>
  );
}

export default function Hero3D() {
  const reduced = usePrefersReducedMotion();
  const dpr = useMemo<[number, number]>(() => [1, 1.75], []);

  return (
    <Canvas
      dpr={dpr}
      camera={{ position: [0, 0, 7.5], fov: 42 }}
      gl={{ antialias: true, alpha: true }}
      className="!absolute inset-0"
    >
      <ambientLight intensity={0.65} />
      <directionalLight position={[3, 4, 5]} intensity={0.7} />
      <directionalLight position={[-4, -2, -3]} intensity={0.25} />
      <OrbitingLight reduced={reduced} />
      <Scene reduced={reduced} />
    </Canvas>
  );
}
