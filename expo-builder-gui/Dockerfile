# syntax=docker/dockerfile:1.7
# Pinned to a specific major version rather than a rolling "lts" tag — see
# orchestrator/Dockerfile for why (native-module prebuilt-binary lag on a brand-new
# LTS). Next.js itself has no native deps, but keeping both images on the same Node
# major avoids any future surprise.
FROM node:22-alpine AS deps
WORKDIR /app
# Patch any CVEs already fixed upstream for packages baked into the node:22-alpine
# base layer itself (not just what we explicitly apk add elsewhere).
RUN apk upgrade --no-cache
COPY package.json package-lock.json* ./
RUN npm install

FROM deps AS build
WORKDIR /app
COPY . .
# NEXT_PUBLIC_* variables are inlined into the client bundle at *build* time, but this
# image is built once and published for many different users, each potentially
# choosing a different orchestrator port at `docker run`/`ebl start` time. So we bake
# in a fixed, distinctive placeholder here instead of a real URL — docker-entrypoint.sh
# substitutes the real value into the compiled output at container start, from the
# ORCHESTRATOR_URL env var. (Local docker-compose deployments use the same mechanism —
# see docker-compose.yml's `web.environment.ORCHESTRATOR_URL`.)
ENV NEXT_PUBLIC_ORCHESTRATOR_URL=http://__EBL_ORCHESTRATOR_URL__
RUN npm run build

FROM node:22-alpine AS runtime
WORKDIR /app
ENV NODE_ENV=production
RUN apk upgrade --no-cache
COPY --from=build /app/public ./public
COPY --from=build /app/.next/standalone ./
COPY --from=build /app/.next/static ./.next/static
COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

EXPOSE 3000
ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
