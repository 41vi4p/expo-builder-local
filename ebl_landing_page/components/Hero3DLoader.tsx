"use client";

import dynamic from "next/dynamic";

const Hero3D = dynamic(() => import("./Hero3D"), {
  ssr: false,
  loading: () => <div className="absolute inset-0" aria-hidden />,
});

export default function Hero3DLoader() {
  return (
    <div className="relative h-full w-full" aria-hidden="true">
      <Hero3D />
    </div>
  );
}
