#include "setup.hpp"

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>

#include "../color.hpp"
#include "../config_store.hpp"
#include "../docker_client.hpp"
#include "../pull_progress.hpp"

namespace ebl::commands {

namespace {

#ifdef _WIN32
void printUsage() {
  std::cout << R"(ebl setup

One-time setup: makes sure Docker Desktop is reachable, then pulls the runner/
orchestrator/web images so `ebl build`/`ebl start` are ready to go immediately.

Options:
  -h, --help   Show this help
)";
}
#else
void printUsage() {
  std::cout << R"(ebl setup

One-time setup: makes sure Docker is installed and running, offers to install it if
not (official convenience script, requires sudo — also enables+starts the systemd
service and adds you to the docker group, which the script alone doesn't do), then
pulls the runner/orchestrator/web images so `ebl build`/`ebl start` are ready to go
immediately.

Options:
  -h, --help   Show this help
)";
}

bool commandExists(const std::string& name) {
  int status = std::system(("command -v " + name + " >/dev/null 2>&1").c_str());
  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool promptYesNo(const std::string& question) {
  std::cout << question << " [y/N] " << std::flush;
  std::string line;
  if (!std::getline(std::cin, line)) return false;
  return line == "y" || line == "Y" || line == "yes" || line == "Yes";
}
#endif

}  // namespace

void printSetupUsage() { printUsage(); }

int runSetup(int argc, char** argv) {
  for (int i = 0; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      printUsage();
      return 0;
    }
  }

  ebl::DockerClient docker("/var/run/docker.sock");

  std::cout << ebl::color::bold("Checking Docker...") << "\n";
  if (!docker.ping()) {
#ifdef _WIN32
    // No convenience-script auto-install path here — Docker Desktop is a GUI
    // installer with its own license/reboot considerations, same stance
    // install.ps1 already takes before it even gets this far.
    std::cerr << ebl::color::red("Docker Desktop isn't reachable.")
              << " Install/start it from https://www.docker.com/products/docker-desktop/ , then re-run `ebl setup`.\n";
    return 1;
#else
    if (!commandExists("docker")) {
      std::cout << "Docker doesn't appear to be installed.\n";
      if (!promptYesNo("Install it now via the official convenience script (curl -fsSL "
                        "https://get.docker.com | sh)? This will ask for sudo.")) {
        std::cout << "Skipped. Install Docker yourself (https://docs.docker.com/engine/install/) "
                     "and re-run `ebl setup`.\n";
        return 1;
      }
      std::cout << ebl::color::dim("Running the Docker install script...") << "\n";
      int status = std::system("curl -fsSL https://get.docker.com | sh");
      if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        std::cerr << ebl::color::red("Docker install script failed — install it manually and re-run `ebl setup`.")
                  << "\n";
        return 1;
      }
      std::cout << ebl::color::green("Docker installed.") << "\n";

      // get.docker.com installs the packages but, unlike the manual apt flow, doesn't
      // enable/start the systemd service or add the invoking user to the docker group
      // — do both explicitly so a fresh install is actually usable, not just present.
      int ignored;
      ignored = std::system("sudo systemctl enable docker >/dev/null 2>&1");
      ignored = std::system("sudo systemctl start docker >/dev/null 2>&1");
      (void)ignored;
      if (const char* sudoUser = std::getenv("USER"); sudoUser && *sudoUser) {
        std::string cmd = std::string("sudo usermod -aG docker '") + sudoUser + "'";
        int groupStatus = std::system(cmd.c_str());
        if (WIFEXITED(groupStatus) && WEXITSTATUS(groupStatus) == 0) {
          std::cout << ebl::color::dim("Added \"" + std::string(sudoUser) + "\" to the docker group.") << "\n";
        }
      }

      if (!docker.ping()) {
        std::cout << ebl::color::yellow(
                          "Docker is installed but not reachable from this shell yet — you likely need to log "
                          "out and back in (or run `newgrp docker`) so your user picks up docker-group "
                          "membership, then re-run `ebl setup`.")
                  << "\n";
        return 1;
      }
    } else {
      std::cout << "Docker is installed but the daemon isn't reachable — trying to start it "
                   "(sudo systemctl start docker)...\n";
      int ignored;
      ignored = std::system("sudo systemctl enable docker >/dev/null 2>&1");
      ignored = std::system("sudo systemctl start docker >/dev/null 2>&1");
      (void)ignored;
      if (!docker.ping()) {
        std::cerr << ebl::color::red(
                          "Still not reachable after trying to start it. Check its status yourself: "
                          "sudo systemctl status docker — then re-run `ebl setup`.")
                  << "\n";
        return 1;
      }
      std::cout << ebl::color::green("Docker started.") << "\n";
    }
#endif
  }
  std::cout << ebl::color::green("Docker is up.") << "\n\n";

  auto cfg = ebl::loadConfig().value_or(ebl::EblConfig{});

  std::cout << ebl::color::bold("Pulling images...") << "\n";
  bool anyFailed = false;
  for (const auto& tag : {cfg.runnerImage(), cfg.orchestratorImage(), cfg.webImage()}) {
    std::cout << ebl::color::dim("Pulling " + tag + "...") << "\n";
    try {
      ebl::PullProgressRenderer progress;
      docker.pullImage(tag, [&progress](const std::string& id, const std::string& status,
                                         const std::string& p) { progress.onEvent(id, status, p); });
    } catch (const std::exception& e) {
      std::cout << ebl::color::yellow("Could not pull " + tag + ": " + e.what()) << "\n";
      std::cout << ebl::color::dim(
                        "(Not published yet? `ebl build`/`ebl start` will build the runner image locally "
                        "the first time it's needed; the orchestrator/web images must exist somewhere for "
                        "`ebl start` to work.)")
                << "\n";
      anyFailed = true;
    }
  }

  cfg.setupCompletedAt = static_cast<int64_t>(time(nullptr));
  ebl::saveConfig(cfg);

  std::cout << "\n";
  if (anyFailed) {
    std::cout << ebl::color::yellow("Setup finished with some images not pulled (see above).") << "\n";
  } else {
    std::cout << ebl::color::green(ebl::color::bold("Setup complete.")) << "\n";
  }
  std::cout << "Next: " << ebl::color::cyan("ebl config") << " to set your projects folder and Expo token, then "
            << ebl::color::cyan("ebl start") << ".\n";
  return anyFailed ? 1 : 0;
}

}  // namespace ebl::commands
