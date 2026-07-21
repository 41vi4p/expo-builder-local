# syntax=docker/dockerfile:1.7
FROM node:lts-alpine AS deps
WORKDIR /app
COPY package.json package-lock.json* ./
RUN npm install

FROM deps AS build
WORKDIR /app
COPY . .
# NEXT_PUBLIC_* variables are inlined into the client bundle at build time, so the
# orchestrator's browser-facing URL must be known here, not just at container-run
# time. docker-compose.yml passes it as a build arg (see ORCHESTRATOR_PUBLIC_URL).
ARG NEXT_PUBLIC_ORCHESTRATOR_URL=http://localhost:4001
ENV NEXT_PUBLIC_ORCHESTRATOR_URL=${NEXT_PUBLIC_ORCHESTRATOR_URL}
RUN npm run build

FROM node:lts-alpine AS runtime
WORKDIR /app
ENV NODE_ENV=production
COPY --from=build /app/public ./public
COPY --from=build /app/.next/standalone ./
COPY --from=build /app/.next/static ./.next/static

EXPOSE 3000
CMD ["node", "server.js"]
