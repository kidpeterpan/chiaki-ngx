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

### T16. 22 HIGH and CRITICAL CVEs in the bundled curl (resolved 2026-11-25)

- Was: osv-scanner reported 8 CRITICAL and 14 HIGH CVEs against `third-party/curl` pinned at
  `75a2079` (curl 8.11.1, 2024-12-11). They carried a CVSS score but no severity label, so the
  audit had counted them as unrated (T7).
- Resolved: the submodule is at `curl-8_23_0` (`3163ec06`) and the audit reports none of them.
  Two follow-ups came with it: `CURL_USE_LIBPSL OFF` in `third-party/CMakeLists.txt` (curl 8.x
  requires libpsl by default; cookie domains are not used), and `cmake/curl-schannel-aia.patch`
  regenerated for the new sources (same 40 changed lines; only context moved). The Windows
  build that applies that patch has not been run locally.
- The new tree's own HTTP test-suite requirements bring `filelock` 3.32.7 (GHSA-v4x8-3wr5-3wq8,
  HIGH). The build never installs them; the exception is proposed in
  `scripts/verify/audit-exceptions.txt` for the repo owner's review.

### T17. Chrome Screen AI aborted the app on any pipeline restart (resolved 2026-11-25)

- Symptom (Steam Deck): with OCR set to Chrome Screen AI, closing the translator settings,
  turning the translator off and on, or switching OCR away and back closed the app.
- Cause: every pipeline restart built a new `ScreenAiEngine`, which called
  `InitOCRUsingCallback` again. libchromescreenai.so aborts on a second initialization
  (`ocr.cc:539] Check failed: !pipeline_`, SIGABRT), and `dlclose` does not reset it.
  Reproduced on x86_64 Linux with the real library.
- Fix: `gui/src/translator/screenai.cpp` loads and initializes the library once per process
  and never closes it; engines share it.
- Test: `translator_screenai_restart_tests` (five restarts, then switch away and back, each
  step must read the fixture text). Registered on Linux once
  `scripts/translator/fetch-screenai.sh` has fetched the library, so `test-gui` runs it there.
  It aborts on the previous code. Not run on macOS or in CI, which have no x86_64 Linux job
  with the library.

## High

### T3. Bundled curl needs OpenSSL 3, while one source file needs OpenSSL 4 symbols

- Evidence: with OpenSSL 4 headers visible, the bundled curl fails to build (ENGINE API
  removed in 4). `lib/src/aia_openssl.c` references `OPENSSL_sk_set_cmp_thunks` and
  `OPENSSL_sk_set_copy_thunks`, which exist only in OpenSSL 4 headers.
- The harness's `test-unit` stage links both libcrypto builds to get a binary that links.
  The repo's macOS CI (for example `.github/workflows/build-macos-arm.yml`) instead runs `brew unlink openssl@4`,
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

### T7. Advisories without a severity rating (mostly resolved 2026-11-25)

- Was: 46 advisories had no severity label and were counted, not failed.
- Resolved: the audit now falls back to the CVSS base score osv-scanner reports (9.0+
  CRITICAL, 7.0+ HIGH). That surfaced the curl CVEs in T16. One advisory has neither a
  label nor a score and is still only counted.

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

### T15. The translator cloud backend tests are not run by `--full` (resolved 2026-11-25)

- Resolved: the `test-gui` stage configures a tests-on tree in `build-gui-tests/`, builds only
  the `translator_*` test targets (read back from ctest) and runs every translator suite,
  including the controller-driven QML panel suite. Original finding below.


- Evidence: `gui/tests/translator/TestCloudBackends.cpp` (target `translator_cloud_tests`, a
  local mock server, no network) is defined in `gui/CMakeLists.txt` only when the GUI and
  `CHIAKI_ENABLE_TESTS` are on. `scripts/verify/stages/test-unit.sh` configures with
  `CHIAKI_ENABLE_GUI=OFF` and `CHIAKI_ENABLE_TRANSLATOR=OFF`, and `build-release` builds with
  tests off. `build-release` compiles `cloudbackends.cpp` but runs nothing against it.
- Also found: the top-level `enable_testing()` ran after `add_subdirectory(gui)`, so the target
  was never registered with ctest. `gui/CMakeLists.txt` now calls `enable_testing()` itself.
- Manual run: configure a tree with `-DCHIAKI_ENABLE_TESTS=ON` (GUI and translator on), then
  `cmake --build <dir> --target translator_cloud_tests` and
  `ctest --test-dir <dir> -R translator_cloud_tests`.
- Action: add a harness step that builds and runs the target.

## Working-tree state (not a defect, but affects the next commit)

- `scripts/verify-health.sh` is tracked in HEAD (it came in with commit `6107c78e`), and its
  deletion is unstaged. The new harness replaces it. Commit the deletion with the rest.
- `.github/workflows/verify.yml`, `Makefile`, `scripts/verify.sh`, `scripts/verify/`,
  `AGENTS.md`, and the `.gitignore` change are not committed.
- The harness assumes the repo's current file names. After any rename, rerun
  `scripts/verify.sh --stage lint`.
