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
	@echo "  make install-cli  Build (CMake/C++) and install the 'ebl' CLI to ~/.local/bin"
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

# Built out-of-tree in ~/.cache rather than cli/build: if this checkout sits on a
# slow or network-backed filesystem, CMake's own configure/build (many small file
# writes — object files, compiler feature checks) can be dramatically slower there
# than on your actual root filesystem. Building elsewhere sidesteps that entirely;
# CLI_BUILD_DIR is overridable if you'd rather build in-tree.
CLI_BUILD_DIR ?= $(HOME)/.cache/expo-builder-local/cli-build

install-cli:
	cmake -S cli -B $(CLI_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$(HOME)/.local
	cmake --build $(CLI_BUILD_DIR) -j
	cmake --install $(CLI_BUILD_DIR)
	@echo ""
	@echo "Installed to $(HOME)/.local/bin/ebl"
	@echo "Make sure ~/.local/bin is on your PATH, then try: ebl --help"

deb:
	cmake -S cli -B $(CLI_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(CLI_BUILD_DIR) -j
	cd $(CLI_BUILD_DIR) && cpack -G DEB
	@echo ""
	@echo "Package(s):"
	@ls -1 $(CLI_BUILD_DIR)/*.deb

publish-images:
	./scripts/publish-images.sh

publish-images-push:
	./scripts/publish-images.sh --push
