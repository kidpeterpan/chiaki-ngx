# Verification triage

Pre-existing defects and gaps found while building `scripts/verify.sh`. Each one is a real
finding in the tree or the environment. None was fixed by weakening a check. Status is as of
the last full run; update it when an item changes.

Severity: **blocking** = `--full` fails because of it. **high** = a real risk to users or
builds. **medium** = a gap in coverage or reproducibility. **low** = noise.

## Blocking: `--full` fails

### T1. Client secret committed in source (accepted 2026-11-24)

- Evidence: gitleaks `generic-api-key` at `gui/include/psnaccountid.h:19` (`CLIENT_SECRET`)
  and `scripts/psn-account-id.py:52`.
- This is an OAuth client secret for the PlayStation Network login flow, committed in plain
  text in a public repository.
- Options: (a) accept it as a public app credential and allowlist it in
  `scripts/verify/gitleaks.toml` with that reason, on record; (b) remove it from the tree and
  load it from configuration. Option (b) changes the login flow, so it is a product decision.
- The harness does not decide this. Until it is decided, the secret scan fails.

### T2. HIGH advisories in a vendored dev lock file (accepted until 2027-02-22)

- Evidence: `third-party/nanopb/extra/requirements_lock.txt`:
  - `protobuf 5.29.5`, GHSA-7gcm-g887-7qv7, HIGH (JSON recursion depth bypass).
  - `setuptools 70.0.0`, GHSA-5rjg-fvgr-3xxf, HIGH (path traversal in `PackageIndex.download`).
- Scope: the file is nanopb's own development requirements. It is not installed by this
  project's build. The harness's venv installs its own pinned packages
  (`scripts/verify/requirements.txt`, which has no advisories).
- Fix options: bump the pins upstream in nanopb (a vendored tree, so not edited here), or add
  dated entries to `scripts/verify/audit-exceptions.txt`. The second option is a decision for a
  reviewer.

## High

### T3. Bundled curl needs OpenSSL 3, while one source file needs OpenSSL 4 symbols

- Evidence: with OpenSSL 4 headers visible, the bundled curl fails to build (ENGINE API
  removed in 4). `lib/src/aia_openssl.c` references `OPENSSL_sk_set_cmp_thunks` and
  `OPENSSL_sk_set_copy_thunks`, which exist only in OpenSSL 4 headers.
- The harness's `test-unit` stage links both libcrypto builds to get a binary that links.
  The repo's CI (`.github/workflows/build-macos.yml`) instead runs `brew unlink openssl@4`,
  so only OpenSSL 3 is visible.
- Root cause to check: `aia_openssl.c` likely compiles against `/opt/homebrew/include`, which
  holds the OpenSSL 4 headers, because include directories are ordered before `CMAKE_C_FLAGS`.
- Decision (2026-11-24): keep the two-libcrypto link. The harness does not run `brew unlink`.
  Matching CI by unlinking openssl@4 remains an option for later.
- Risk of the current workaround: the test binary mixes two OpenSSL ABIs. A green
  `test-unit` is weaker evidence than it looks until the header order is fixed.

### T4. Windowed GUI start is not covered by the harness

- The `smoke` stage runs `--help` only. A real window launch would be needed to catch
  startup regressions.
- Why it is not automated here: `--profile` persistently changes the user's current profile
  (`gui/src/main.cpp`, `Settings::SetCurrentProfile`). A timed kill during Vulkan startup
  latches the OpenGL fallback (`gui/src/qmlmainwindow.cpp`, `vulkan_startup_pending`). Both
  change the user's real app state.
- Fix option: run the windowed smoke under a separate macOS user account, or give the app an
  isolated settings location for tests.

### T5. Duplicate CI for the translator (decided 2026-11-24: keep all three)

- `translator-tests.yml` (Linux without ONNX, and macOS with ONNX) and `build-macos.yml`
  overlap with `verify.yml`. Each runs part of what `--full` runs.
- Decision (2026-11-24): keep all three workflows. Nothing was removed or changed.

### T6. The dependency audit cannot see Homebrew-installed libraries

- osv-scanner reads lock files and submodule commits. It does not see Homebrew formulas
  (Qt, FFmpeg, ONNX Runtime, OpenSSL, and others), which the app links against.
- The audit covers source and vendored code only. A shipped binary's dependencies need a
  separate check.

## Medium

### T7. 46 advisories have no severity rating

- These are counted and not failed, per the threshold policy. Most are in the bundled curl
  submodule (`third-party/curl`, pinned at `75a2079`), which has about 100 advisory entries.
- Action: check whether the pinned curl is current. Consider a newer curl submodule commit.

### T8. Shellcheck findings in upstream scripts

- 51 warnings in `scripts/*.sh` and subfolders (for example `scripts/build-ffmpeg.sh`,
  `scripts/build-appimage.sh`). These files are upstream's, so they are outside the
  enforced lint scope (`scripts/translator/`, the harness).
- Action: fix them, or widen the enforced scope once they are clean.

### T9. actionlint findings in upstream workflows

- 26 findings across `.github/workflows/*` other than `verify.yml`, mostly shellcheck style
  inside `run:` blocks (for example backticks in `build-appimage-*.yml`).
- Action: same as T8.

### T10. qmllint cannot resolve the app's QML module

- `TranslatorOverlay.qml` cannot import `org.streetpea.chiaking`, because qmllint has no
  import path to the build tree. As a result it reports "unqualified access" for the
  `Translator` and `Chiaki` context objects, and those warnings are not checked.
- Errors fail the stage, and the current count is zero. Warnings are logged.
- Action: pass `-I` to the built QML module so the import resolves, then treat warnings as
  errors.

### T11. Reproducibility of the tools (partly addressed)

- gitleaks was installed with `go install`, so it has no version stamp
  (`gitleaks version` prints "version is set by build process"). CI installs it from Homebrew,
  which floats.
- Addressed: every run now records exact versions in
  `.verify-logs/<run-id>/tool-versions.txt`, so a dev/CI disagreement is visible.
- Still open: the versions themselves are not pinned. Homebrew and apt both float.

### T12. The lib unit tests are one ctest entry

- `ctest` sees one test, `chiaki-unit`, which runs every munit suite. A failure reports the
  whole binary, not the suite that failed. The munit output inside the log shows the detail.
- Action: register each suite as its own ctest entry.

### T13. Network isolation is by convention

- The unit tests open a loopback UDP socket (`test/takion.c`) and use an `.invalid` URL
  (`test/aia.c`). Nothing blocks external access. The harness does not claim network
  isolation.

### T14. Linux coverage is partial (verified for the core stages only)

- The harness runs on macOS and Linux. Verified on Linux (Debian aarch64, container):
  `env`, `build-core` and `test-core` pass with `TRANS_ONNX=OFF`, and a missing scanner
  produces the install hint rather than a bare "command not found".
- Not verified on Linux: `build-release`, `test-unit`, `smoke` and `security`. They need the
  Qt stack, ONNX Runtime and the scanners, which were not installed in the test container.
  CI's Linux job covers the core stages only; the full gate runs on macOS.
- Action: extend the Linux CI job once a Qt image or cached dependency set is available.

## Working-tree state (not a defect, but affects the next commit)

- `scripts/verify-health.sh` is tracked in HEAD (it came in with commit `6107c78e`), and its
  deletion is unstaged. The new harness replaces it. Commit the deletion with the rest.
- `.github/workflows/verify.yml`, `Makefile`, `scripts/verify.sh`, `scripts/verify/`,
  `AGENTS.md`, and the `.gitignore` change are not committed.
- The harness assumes the repo's current file names. After any rename, rerun
  `scripts/verify.sh --stage lint`.
