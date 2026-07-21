# syntax=docker/dockerfile:1.7
FROM node:lts-alpine AS deps
WORKDIR /app
# better-sqlite3 ships prebuilt binaries for most platforms; these toolchain packages
# are a fallback in case npm has to compile it from source for this platform/Node combo.
RUN apk add --no-cache python3 make g++
COPY package.json package-lock.json* ./
RUN npm install

FROM deps AS build
COPY tsconfig.json ./
COPY src ./src
RUN npm run build

FROM node:lts-alpine AS runtime
WORKDIR /app
ENV NODE_ENV=production
# `git` is used (best-effort, non-fatal if absent) to attach commit/branch metadata to
# each build's metrics; `docker-cli` is NOT needed — we talk to the daemon over the
# mounted socket via dockerode, not the CLI.
RUN apk add --no-cache git
COPY package.json package-lock.json* ./
RUN apk add --no-cache --virtual .build-deps python3 make g++ \
    && npm install --omit=dev \
    && apk del .build-deps
COPY --from=build /app/dist ./dist

EXPOSE 4001
CMD ["node", "dist/server.js"]
