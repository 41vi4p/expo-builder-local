# Releasing the CLI

Pushing a tag matching `v*` (e.g. `v0.3.1`) triggers
[`.github/workflows/release.yml`](../.github/workflows/release.yml) ("Build and
publish APT repository"), which:

1. Builds `ebl` with a static (`CGO_ENABLED=0`) `go build`, then packages it with
   [`nfpm`](https://github.com/goreleaser/nfpm) (`packaging/deb/nfpm.yaml`) — no
   dependency auto-detection needed, since a static Go binary declares zero runtime
   deps (`depends: []` in that config).
2. Assembles a real APT repository tree (`pool/`, `dists/stable/...`) and **signs the
   `Release` file** with GPG — the way `apt` verifies any repository, checked
   automatically on every install once a user has added the repo, not just an
   optional manual check.
3. Publishes that tree to the `gh-pages` branch under `/apt`, served via GitHub Pages
   at `https://41vi4p.github.io/expo-builder-local/apt`.
4. Also stages a plain `bin/` tarball and creates a GitHub Release with the
   `.deb`, the tarball, and a `SHA256SUMS.txt` — a direct-download fallback for
   anyone who'd rather not add the repo.
5. A second job, `windows-build-and-publish`, then builds `cli/` natively for
   Windows (plain `go build`, no vcpkg/MSVC — the Windows named-pipe Docker
   transport is pure Go via `go-winio`), regenerates the icon/version resource
   (`goversioninfo`, see `cli/cmd/ebl/main.go`'s `//go:generate` comment), zips the
   resulting `ebl.exe` as `ebl-windows-amd64.zip`, builds `windows/installer`
   (`ebl-setup-<version>.exe`, an Inno Setup GUI installer bundling that same binary
   plus `install.ps1`/`uninstall.ps1`), and attaches both to the *same* release via
   `gh release upload`. Runs on `windows-latest`; needs the Linux job to finish
   first since the release has to already exist. `windows/install.ps1` — both the
   standalone one-line installer and what `ebl-setup-<version>.exe` runs under the
   hood — downloads `ebl-windows-amd64.zip` from
   `releases/latest/download/ebl-windows-amd64.zip`, so skipping this job (or a
   failed Windows build) breaks that download until the next successful tag.

None of the signing steps run without the one-time setup in
**[`APT_REPO_SETUP_GUIDE.md`](./APT_REPO_SETUP_GUIDE.md)** (generate a GPG key,
register `GPG_PRIVATE_KEY` as a repo secret, commit `docs/apt/pubkey.gpg`, enable
GitHub Pages) — do that first if you haven't. If the signing key isn't configured yet,
the workflow fails fast with a clear `::error::` rather than quietly publishing
something unsigned.

## Cutting a release

```bash
git tag v0.3.1
git push origin v0.3.1
```

Run `scripts/bump-version.sh <patch|minor|major|X.Y.Z>` *before* tagging — it bumps
every canonical version field in one shot; see `../CLAUDE.md`'s version-management
section for what it covers and what's still manual (the `docs/CHANGELOG.md` entry,
and `packaging/arch/PKGBUILD`'s `sha256sums`, computed after tagging - see "Arch
Linux packaging" below). The tag itself is what's authoritative for the GitHub
Release name; keep it in step with `cli/VERSION` so `ebl --version` and the release
tag never disagree on every platform — `ebl.exe` is the same `cli/` Go module as
Linux/macOS, just compiled for Windows, so there's no separate launcher version to
track anymore.

You can also trigger the workflow manually (`workflow_dispatch`, e.g. from the
Actions tab) to republish the current `main` without cutting a new tag — useful for
testing the pipeline itself, or re-publishing after fixing something in the workflow.

## Arch Linux packaging

Unlike the .deb/Windows artifacts above, [`packaging/arch/PKGBUILD`](../packaging/arch/PKGBUILD)
is **not built by this workflow** — it builds `cli/` from source with the
installing machine's own Go toolchain at install time (via `makepkg`, either
directly or through `install.sh`'s pacman-detection path; `CGO_ENABLED=0` still
makes the result fully static, no runtime deps), so there's no separate binary
artifact to publish or sign. Its `pkgver` is bumped by `scripts/bump-version.sh`
alongside the other version fields (see `../CLAUDE.md`#-version-management), but
there's nothing else to do here at release time — `pkgver` pointing at a tag that
doesn't exist yet just means that tag needs to be pushed (this same `git tag`/
`git push` step) before `makepkg` can fetch it.

`sha256sums` is a **real, pinned checksum** of the tagged source archive, not
`SKIP` — there's no per-release AUR-publishing step to keep it in sync
automatically the way the .deb's dpkg-shlibdeps/GPG signing is, so after tagging,
recompute it by hand and update `PKGBUILD`:

```bash
curl -fsSL -o /tmp/ebl-src.tar.gz "https://github.com/41vi4p/expo-builder-local/archive/refs/tags/v<version>.tar.gz"
sha256sum /tmp/ebl-src.tar.gz   # or: updpkgsums (from pacman-contrib), run from packaging/arch/
```

then regenerate `.SRCINFO` from it (`makepkg --printsrcinfo > .SRCINFO` from
`packaging/arch/`, or `scripts/bump-version.sh`'s docker fallback if `makepkg` isn't
installed locally — see its own comment) since the checksum change isn't something
`scripts/bump-version.sh` does for you (it only runs at version-bump time, not after
a checksum-only edit).

If this step gets missed on a given release, set `sha256sums` back to `('SKIP')`
rather than ship a stale/wrong checksum — a mismatched pin makes `makepkg` refuse to
build entirely, whereas `SKIP` just means no integrity check.

`packaging/arch/.SRCINFO` is generated from `PKGBUILD` — regenerate it any time
`PKGBUILD` changes (`scripts/bump-version.sh` does this automatically when it bumps
`pkgver`); it doesn't need network access or a real tag to exist, since it just
parses the script.

**Not AUR-submission-ready yet:**
- `# Maintainer:` in `PKGBUILD` is a placeholder (GitHub profile link, not a real
  name/email) — AUR convention expects the latter.
- Publishing means pushing this directory's contents (`PKGBUILD` + `.SRCINFO`) to
  `ssh://aur@aur.archlinux.org/ebl.git`, which needs an AUR account and its own SSH
  key — a one-time, personal setup step for whoever ends up maintaining it there,
  not something CI can do.
