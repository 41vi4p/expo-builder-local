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
5. A second job, `windows-build-and-publish`, then builds `windows/launcher`
   (`ebl.exe`, the thin WSL2 launcher) and `windows/installer` (`ebl-setup.exe`, an
   Inno Setup GUI installer that just bundles and runs `ebl.exe` +
   `install.ps1`/`uninstall.ps1`), and attaches both to the *same* release via `gh
   release upload`. Runs on `windows-latest`; needs the Linux job to finish first
   since the release has to already exist. `windows/install.ps1` — both the
   standalone one-line installer and what `ebl-setup.exe` runs under the hood —
   downloads `ebl.exe` from `releases/latest/download/ebl.exe`, so skipping this job
   (or a failed Windows build) breaks that download until the next successful tag.

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
`cli/CMakeLists.txt`'s `project(... VERSION ...)`, `windows/launcher/CMakeLists.txt`'s
`project(... VERSION ...)`, and `windows/installer/ebl.iss`'s `MyAppVersion` to match
*before* tagging — see `../CLAUDE.md`'s version-management section. The tag itself is
what's authoritative for the GitHub Release name; keep it in step with the CLI's own
`PROJECT_VERSION` so `ebl --version` and the release tag never disagree (the Windows
launcher doesn't have its own `--version` — it's a thin pass-through — but stays
numbered the same since it only ever ships paired with a specific `ebl` release).

You can also trigger the workflow manually (`workflow_dispatch`, e.g. from the
Actions tab) to republish the current `main` without cutting a new tag — useful for
testing the pipeline itself, or re-publishing after fixing something in the workflow.
