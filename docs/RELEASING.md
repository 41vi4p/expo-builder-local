# Releasing the CLI

Pushing a tag matching `v*` (e.g. `v0.3.1`) triggers
[`.github/workflows/release.yml`](../.github/workflows/release.yml) ("Build and
publish APT repository"), which:

1. Builds `ebl` inside a pinned `ubuntu:24.04` container (CMake/C++17, `cpack -G
   DEB` — dependencies are auto-detected via `dpkg-shlibdeps`, not hand-pinned, so
   they're correct for whatever the container's Ubuntu version actually ships).
2. Assembles a real APT repository tree (`pool/`, `dists/stable/...`) and **signs the
   `Release` file** with GPG — the way `apt` verifies any repository, checked
   automatically on every install once a user has added the repo, not just an
   optional manual check.
3. Publishes that tree to the `gh-pages` branch under `/apt`, served via GitHub Pages
   at `https://41vi4p.github.io/expo-builder-local/apt`.
4. Also stages a plain `bin/`+`share/` tarball and creates a GitHub Release with the
   `.deb`, the tarball, and a `SHA256SUMS.txt` — a direct-download fallback for
   anyone who'd rather not add the repo.
5. A second job, `windows-build-and-publish`, then builds `cli/` natively for
   Windows (MSVC via `cmake`, dependencies from vcpkg — `curl`/`openssl`, static
   triplet so `ebl.exe` ships as a single file with no extra DLLs), `cmake
   --install`s it into a `bin/`+`share/` tree (same layout the `.deb` uses — see
   `cli/src/runner_context.cpp`), zips that tree as `ebl-windows-amd64.zip`, builds
   `windows/installer` (`ebl-setup-<version>.exe`, an Inno Setup GUI installer bundling that
   same tree plus `install.ps1`/`uninstall.ps1`), and attaches both to the *same*
   release via `gh release upload`. Runs on `windows-latest`; needs the Linux job to
   finish first since the release has to already exist. `windows/install.ps1` — both
   the standalone one-line installer and what `ebl-setup-<version>.exe` runs under the hood —
   downloads `ebl-windows-amd64.zip` from
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

Bump `orchestrator/package.json`, `expo-builder-gui/package.json`,
`cli/CMakeLists.txt`'s `project(... VERSION ...)`, `windows/installer/ebl.iss`'s
`MyAppVersion`, and `packaging/arch/PKGBUILD`'s `pkgver` to match *before* tagging —
see `../CLAUDE.md`'s version-management section. The tag itself is what's authoritative for the GitHub Release name; keep it
in step with the CLI's own `PROJECT_VERSION` so `ebl --version` and the release tag
never disagree on every platform — `ebl.exe` is the same `cli/` binary as Linux/macOS,
just compiled for Windows, so there's no separate launcher version to track anymore.

You can also trigger the workflow manually (`workflow_dispatch`, e.g. from the
Actions tab) to republish the current `main` without cutting a new tag — useful for
testing the pipeline itself, or re-publishing after fixing something in the workflow.

## Arch Linux packaging

Unlike the .deb/Windows artifacts above, [`packaging/arch/PKGBUILD`](../packaging/arch/PKGBUILD)
is **not built by this workflow** — it builds `cli/` from source against the
installing machine's own `curl`/`openssl` at install time (via `makepkg`, either
directly or through `install.sh`'s pacman-detection path), so there's no separate
binary artifact to publish or sign. Its `pkgver` is bumped by hand alongside the
other four version fields (see `../CLAUDE.md`#-version-management), but there's
nothing else to do here at release time — `pkgver` pointing at a tag that doesn't
exist yet just means that tag needs to be pushed (this same `git tag`/`git push`
step) before `makepkg` can fetch it.

Its `sha256sums=('SKIP')` is deliberate: this package isn't published to the AUR
(no maintainer account/SSH key for that exists in this project yet), so there's no
per-release step that would otherwise keep a pinned checksum in sync. Before an
eventual AUR submission, replace `SKIP` with the real checksum of that tag's source
archive (`sha256sum` on the same URL curl would fetch, or `updpkgsums` from
`pacman-contrib`).

`packaging/arch/.SRCINFO` is generated from `PKGBUILD` — regenerate it (`makepkg
--printsrcinfo > .SRCINFO` from `packaging/arch/`) any time `PKGBUILD` changes; it
doesn't need network access or a real tag to exist, since it just parses the script.

**Not AUR-submission-ready yet**, beyond the checksum above:
- `# Maintainer:` in `PKGBUILD` is a placeholder (GitHub profile link, not a real
  name/email) — AUR convention expects the latter.
- Publishing means pushing this directory's contents to
  `ssh://aur@aur.archlinux.org/ebl.git`, which needs an AUR account and its own SSH
  key — a one-time, personal setup step for whoever ends up maintaining it there,
  not something CI can do.
