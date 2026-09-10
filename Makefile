.PHONY: help env build-image up down logs restart clean ps install-cli publish-images publish-images-push deb

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
	@echo "  make install-cli  Build (Go) and install the 'ebl' CLI to ~/.local/bin"
	@echo "  make deb          Build a signed-locally-if-configured .deb package for the CLI"
	@echo "  make publish-images       Build the 3 Docker Hub images locally (no push)"
	@echo "  make publish-images-push  Build AND push them — needs DOCKERHUB_NAMESPACE + docker login"

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

CLI_VERSION := $(shell cat cli/VERSION)

install-cli:
	cd cli && ./scripts/sync-runner-assets.sh
	cd cli && CGO_ENABLED=0 go build -ldflags "-X main.version=$(CLI_VERSION)" -o $(HOME)/.local/bin/ebl ./cmd/ebl
	@echo ""
	@echo "Installed to $(HOME)/.local/bin/ebl"
	@echo "Make sure ~/.local/bin is on your PATH, then try: ebl --help"

deb:
	cd cli && ./scripts/sync-runner-assets.sh
	cd cli && CGO_ENABLED=0 go build -ldflags "-X main.version=$(CLI_VERSION)" -o build/ebl ./cmd/ebl
	VERSION=$(CLI_VERSION) nfpm package --config packaging/deb/nfpm.yaml --packager deb --target cli/build/
	@echo ""
	@echo "Package(s):"
	@ls -1 cli/build/*.deb

publish-images:
	./scripts/publish-images.sh

publish-images-push:
	./scripts/publish-images.sh --push
