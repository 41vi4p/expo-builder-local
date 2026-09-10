#!/bin/sh
# One-line installer for the `ebl` CLI (Linux only):
#   curl -fsSL https://raw.githubusercontent.com/41vi4p/expo-builder-local/main/install.sh | sh
#
# Prefers adding the hosted, signed APT repository (so `apt upgrade` picks up future
# releases automatically) when apt/dpkg are available and the repo is reachable. On
# Arch-based distros (pacman present), builds packaging/arch/PKGBUILD from source
# with makepkg instead, for a real pacman-tracked package. Otherwise falls back to
# installing the latest release's .deb directly, then to a plain tarball extracted
# into ~/.local (or /usr/local as root) — no package manager required either way.
set -eu

REPO="41vi4p/expo-builder-local"
APT_REPO_URL="https://41vi4p.github.io/expo-builder-local/apt"
API_URL="https://api.github.com/repos/${REPO}/releases/latest"

log() { printf '%s\n' "$*" >&2; }
die() { log "error: $*"; exit 1; }

command -v curl >/dev/null 2>&1 || die "curl is required"

if [ "$(uname -s)" != "Linux" ]; then
  die "this installer only supports Linux (Linux x86_64); on other platforms, build from source (see cli/go.mod)."
fi
if [ "$(uname -m)" != "x86_64" ]; then
  die "prebuilt releases are x86_64 only; build from source for other architectures."
fi

if [ "$(id -u)" = "0" ]; then
  SUDO=""
elif command -v sudo >/dev/null 2>&1; then
  SUDO="sudo"
else
  SUDO=""
fi

# --- Preferred path: add the hosted APT repo ----------------------------------------
if command -v apt >/dev/null 2>&1 && curl -fsSL -o /dev/null "${APT_REPO_URL}/pubkey.gpg" 2>/dev/null; then
  log "Adding the expo-builder-local APT repository..."
  KEYRING="/usr/share/keyrings/ebl-archive-keyring.gpg"
  curl -fsSL "${APT_REPO_URL}/pubkey.gpg" | ${SUDO} gpg --batch --yes --dearmor -o "${KEYRING}"
  echo "deb [arch=amd64 signed-by=${KEYRING}] ${APT_REPO_URL} stable main" \
    | ${SUDO} tee /etc/apt/sources.list.d/ebl.list >/dev/null

  log "Installing via apt (dependencies + future updates handled automatically)..."
  ${SUDO} apt update -qq
  ${SUDO} apt install -y ebl

  log "Installed. Try: ebl --help"
  log "Future releases: a plain 'sudo apt upgrade' will pick them up."
  exit 0
fi

if command -v apt >/dev/null 2>&1; then
  log "Hosted APT repo not reachable — falling back to a direct download."
fi

# --- Preferred path (Arch-based distros): build + install a native pacman package ---
# Not hosted as a signed repo the way APT is (that would mean maintaining a whole
# second signed-repo pipeline) — instead this builds packaging/arch/PKGBUILD with
# makepkg, straight from source against the distro's own curl/openssl, giving a real
# pacman-tracked package (`pacman -Qi ebl`, `pacman -R ebl`) without needing an AUR
# submission first. See packaging/arch/PKGBUILD's header for details.
if command -v pacman >/dev/null 2>&1; then
  if ! command -v makepkg >/dev/null 2>&1; then
    log "pacman found but makepkg is missing (install the 'base-devel' group for a native package: sudo pacman -S --needed base-devel) — falling back to a direct tarball install."
  elif [ "$(id -u)" = "0" ]; then
    log "makepkg refuses to run as root — re-run this installer as a regular (non-root) user for a native pacman package. Falling back to a direct tarball install for now."
  else
    log "Arch-based distro detected — building the ebl PKGBUILD with makepkg..."
    PKGBUILD_URL="https://raw.githubusercontent.com/${REPO}/main/packaging/arch/PKGBUILD"
    ARCH_TMPDIR="$(mktemp -d)"
    if curl -fsSL -o "${ARCH_TMPDIR}/PKGBUILD" "${PKGBUILD_URL}"; then
      if ( cd "${ARCH_TMPDIR}" && makepkg -si --noconfirm ); then
        rm -rf "${ARCH_TMPDIR}"
        log "Installed via pacman. Try: ebl --help"
        log "Future releases: re-run this installer to rebuild and upgrade."
        exit 0
      fi
      log "makepkg failed — falling back to a direct tarball install."
    else
      log "Could not fetch the PKGBUILD — falling back to a direct tarball install."
    fi
    rm -rf "${ARCH_TMPDIR}"
  fi
fi

# --- Fallback: download the latest release's .deb or tarball directly ---------------
log "Fetching the latest release info from GitHub..."
RELEASE_JSON="$(curl -fsSL "${API_URL}")" || die "could not reach ${API_URL}"

extract_url() {
  # Pulls the first browser_download_url ending in the given suffix out of the
  # release JSON — sed/grep only, so this doesn't need jq installed.
  printf '%s' "${RELEASE_JSON}" \
    | grep -o "\"browser_download_url\": *\"[^\"]*${1}\"" \
    | head -n1 \
    | sed -E 's/.*"(https[^"]+)"/\1/'
}

DEB_URL="$(extract_url '_amd64\.deb')"
TARBALL_URL="$(extract_url 'linux-amd64\.tar\.gz')"

TMPDIR="$(mktemp -d)"
trap 'rm -rf "${TMPDIR}"' EXIT

if command -v dpkg >/dev/null 2>&1 && [ -n "${DEB_URL}" ]; then
  log "Downloading ${DEB_URL}"
  curl -fsSL -o "${TMPDIR}/ebl.deb" "${DEB_URL}"

  [ -n "${SUDO}" ] || [ "$(id -u)" = "0" ] || die "installing the .deb requires root — re-run as root, install sudo, or use the tarball path below"

  log "Installing via apt (resolves libcurl4/libssl3 automatically)..."
  if command -v apt >/dev/null 2>&1; then
    ${SUDO} apt install -y "${TMPDIR}/ebl.deb"
  else
    ${SUDO} dpkg -i "${TMPDIR}/ebl.deb" || ${SUDO} apt-get install -f -y
  fi

  log "Installed. Try: ebl --help"
  exit 0
fi

[ -n "${TARBALL_URL}" ] || die "could not find a release asset to install"

log "dpkg not found (or no .deb asset) — installing the plain tarball instead."
log "Downloading ${TARBALL_URL}"
curl -fsSL -o "${TMPDIR}/ebl.tar.gz" "${TARBALL_URL}"

if [ "$(id -u)" = "0" ]; then
  PREFIX="/usr/local"
else
  PREFIX="${HOME}/.local"
fi
mkdir -p "${PREFIX}"
tar -xzf "${TMPDIR}/ebl.tar.gz" -C "${PREFIX}"

log "Installed to ${PREFIX}/bin/ebl"
case ":${PATH}:" in
  *":${PREFIX}/bin:"*) ;;
  *) log "Note: ${PREFIX}/bin isn't on your PATH — add it in your shell profile." ;;
esac
log "Try: ${PREFIX}/bin/ebl --help"
