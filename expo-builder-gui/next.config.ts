import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Produces .next/standalone — a self-contained server bundle with only the
  // dependencies actually used, so the production Docker image doesn't need a full
  // `npm install` or the whole node_modules tree. See Dockerfile.
  output: "standalone",
};

export default nextConfig;
