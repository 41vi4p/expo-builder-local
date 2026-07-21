.PHONY: help env build-image up down logs restart clean ps

help:
	@echo "expo-builder-local"
	@echo ""
	@echo "  make env          Copy .env.example to .env (only if .env doesn't exist yet)"
	@echo "  make build-image  Build the Android runner image (docker/runner) — large, one-time"
	@echo "  make up           Build the GUI + orchestrator and start them (detached)"
	@echo "  make down         Stop the GUI + orchestrator"
	@echo "  make logs         Follow orchestrator + web logs"
	@echo "  make restart      Recreate the GUI + orchestrator after a code change"
	@echo "  make ps           Show status of the builder's containers"
	@echo "  make clean        Stop everything and remove the data/cache volumes (destructive)"

env:
	@test -f .env || (cp .env.example .env && echo "Created .env — edit it before running 'make up'")

build-image:
	docker compose --profile build-only build runner

up: env
	docker compose up -d --build web orchestrator
	@echo ""
	@echo "GUI:          http://localhost:$${WEB_PORT:-3000}"
	@echo "Orchestrator: http://localhost:$${ORCHESTRATOR_PORT:-4001}/api/health"

down:
	docker compose down

restart:
	docker compose up -d --build web orchestrator

logs:
	docker compose logs -f web orchestrator

ps:
	docker compose ps

clean:
	docker compose down -v
