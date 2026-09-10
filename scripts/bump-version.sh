#!/usr/bin/env bash
# Bumps expo-builder-local's one shared version number across every canonical
# source (see ../CLAUDE.md#-version-management) in one shot, instead of
# hand-editing each file. Usage:
#
#   scripts/bump-version.sh 0.28.2          # set an explicit version
#   scripts/bump-version.sh patch|minor|major   # compute from cli/VERSION
#
# Updates: orchestrator/package.json, expo-builder-gui/package.json,
# cli/VERSION, windows/installer/ebl.iss, packaging/arch/PKGBUILD (pkgver,
# resets pkgrel to 1), packaging/arch/.SRCINFO (regenerated),
# cli/cmd/ebl/versioninfo.json, and cli/cmd/ebl/resource_windows_amd64.syso
# (regenerated, best-effort - needs `go` + network).
#
# Does NOT touch docs/CHANGELOG.md - write that entry by hand, its content
# can't be generated. Does NOT touch packaging/arch/PKGBUILD's sha256sums -
# that's a real checksum of the tagged source archive, computed after
# tagging (see docs/RELEASING.md's "Arch Linux packaging" section).
set -euo pipefail

usage() {
  echo "Usage: $0 <X.Y.Z | patch | minor | major>" >&2
  exit 1
}

[ $# -eq 1 ] || usage

cd "$(dirname "${BASH_SOURCE[0]}")/.."   # repo root
OLD_VERSION="$(cat cli/VERSION)"

case "$1" in
  patch|minor|major)
    IFS='.' read -r MAJOR MINOR PATCH <<< "$OLD_VERSION"
    case "$1" in
      patch) PATCH=$((PATCH + 1)) ;;
      minor) MINOR=$((MINOR + 1)); PATCH=0 ;;
      major) MAJOR=$((MAJOR + 1)); MINOR=0; PATCH=0 ;;
    esac
    NEW_VERSION="${MAJOR}.${MINOR}.${PATCH}"
    ;;
  *)
    NEW_VERSION="${1#v}"
    ;;
esac

if ! [[ "$NEW_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  echo "error: version must be X.Y.Z (got: $1)" >&2
  usage
fi

if [ "$NEW_VERSION" = "$OLD_VERSION" ]; then
  echo "error: $NEW_VERSION is already the current version (cli/VERSION)" >&2
  exit 1
fi

echo "Bumping expo-builder-local: $OLD_VERSION -> $NEW_VERSION"

# 1-2. orchestrator + GUI package.json
# chmod --reference: jq's output redirect creates a fresh file (default umask
# perms), which silently dropped orchestrator/package.json's executable bit
# the first time this ran - restore whatever mode the file already had.
for f in orchestrator/package.json expo-builder-gui/package.json; do
  jq --arg v "$NEW_VERSION" '.version = $v' "$f" > "$f.tmp"
  chmod --reference="$f" "$f.tmp"
  mv "$f.tmp" "$f"
done

# 3. cli/VERSION
echo "$NEW_VERSION" > cli/VERSION

# 4. windows/installer/ebl.iss
sed -i "s/^#define MyAppVersion \".*\"/#define MyAppVersion \"${NEW_VERSION}\"/" windows/installer/ebl.iss

# 5. packaging/arch/PKGBUILD - pkgver bump resets pkgrel to 1 (Arch convention:
# pkgrel only climbs for packaging-only fixes against the same pkgver - see
# ../CLAUDE.md#-version-management)
sed -i \
  -e "s/^pkgver=.*/pkgver=${NEW_VERSION}/" \
  -e "s/^pkgrel=.*/pkgrel=1/" \
  packaging/arch/PKGBUILD

# 6. cli/cmd/ebl/versioninfo.json (Windows exe's embedded FixedFileInfo/StringFileInfo)
IFS='.' read -r V_MAJOR V_MINOR V_PATCH <<< "$NEW_VERSION"
jq --argjson major "$V_MAJOR" --argjson minor "$V_MINOR" --argjson patch "$V_PATCH" --arg v "$NEW_VERSION" '
  .FixedFileInfo.FileVersion = {Major:$major, Minor:$minor, Patch:$patch, Build:0} |
  .FixedFileInfo.ProductVersion = {Major:$major, Minor:$minor, Patch:$patch, Build:0} |
  .StringFileInfo.FileVersion = $v |
  .StringFileInfo.ProductVersion = $v
' cli/cmd/ebl/versioninfo.json > cli/cmd/ebl/versioninfo.json.tmp
chmod --reference=cli/cmd/ebl/versioninfo.json cli/cmd/ebl/versioninfo.json.tmp
mv cli/cmd/ebl/versioninfo.json.tmp cli/cmd/ebl/versioninfo.json

# 7. Regenerate the Windows icon/version resource - best-effort, needs `go` +
# network. Must run from cmd/ebl/ itself: goversioninfo resolves
# versioninfo.json's IconPath relative to its own cwd, not the json file's
# location (see docs/CHANGELOG.md's v0.28.1 entry for the bug this caused
# when a CI step got this wrong).
if command -v go >/dev/null 2>&1; then
  echo "Regenerating cli/cmd/ebl/resource_windows_amd64.syso..."
  if ! (cd cli/cmd/ebl && go run github.com/josephspurrier/goversioninfo/cmd/goversioninfo@v1.7.0 -o resource_windows_amd64.syso versioninfo.json); then
    echo "warning: could not regenerate resource_windows_amd64.syso - do it by hand (see cmd/ebl/main.go's //go:generate comment)" >&2
  fi
else
  echo "warning: 'go' not found - cli/cmd/ebl/resource_windows_amd64.syso NOT regenerated, do it by hand" >&2
fi

# 8. Regenerate packaging/arch/.SRCINFO - never hand-edit it (see ../CLAUDE.md)
echo "Regenerating packaging/arch/.SRCINFO..."
if command -v makepkg >/dev/null 2>&1; then
  (cd packaging/arch && makepkg --printsrcinfo > .SRCINFO)
elif command -v docker >/dev/null 2>&1; then
  docker run --rm -v "$PWD/packaging/arch":/pkg -w /pkg archlinux:latest bash -c '
    pacman -Sy --noconfirm --needed base-devel >/dev/null 2>&1
    id builduser >/dev/null 2>&1 || useradd -m builduser
    chown -R builduser:builduser /pkg
    su builduser -c "makepkg --printsrcinfo" > /pkg/.SRCINFO
  '
else
  echo "warning: neither 'makepkg' nor 'docker' found - packaging/arch/.SRCINFO NOT regenerated, do it by hand (see docs/RELEASING.md)" >&2
fi

echo ""
echo "Done: ${OLD_VERSION} -> ${NEW_VERSION}"
echo ""
echo "Still needed by hand:"
echo "  - Add a new entry at the top of docs/CHANGELOG.md (format in ../CLAUDE.md)"
echo "  - packaging/arch/PKGBUILD's sha256sums - only after tagging, see docs/RELEASING.md"
