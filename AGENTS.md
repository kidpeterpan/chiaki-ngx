# AGENTS.md

Operating rules for AI coding agents in this repository. `CLAUDE.md` points here.

## Commands

```bash
scripts/verify.sh --quick          # inner loop, ~5 s warm: env, lint, build-core, test-core
scripts/verify.sh --full           # before concluding any task (default when no mode is given)
scripts/verify.sh --stage NAME     # one stage plus its prerequisites; repeatable
scripts/verify.sh --full --json    # machine-readable summary on stdout
make verify | verify-quick | verify-json | verify-stage STAGE=NAME   # same commands
```

Stages, in order: `env lint build-core test-core build-release test-unit smoke security`.

`--full` cleans the build directories the harness owns (`build-core/`, `build-release/`,
`build-unit/`). Measured on the development Mac: about 80 s end to end. It never touches
`build-macos/`, which is the user's own build.

## Reading a result

- Each stage prints `PASS` or `FAIL` with its duration. Passing output is not printed.
- On `FAIL` you get the failing step, at most 80 lines of excerpt, and the full log path.
- Full logs: `.verify-logs/latest/<stage>.log`. `.verify-logs/<run-id>/summary.json` has the
  same data in JSON. The last 20 runs are kept.
- Exit codes: `0` PASS, `1` a verification stage failed, `2` an environment prerequisite is
  missing (the `env` stage failed), `64` usage error, `130`/`143` interrupted.
- A `SKIP` row means a stage was not run because a prerequisite failed. It counts as a failure.

Read the log before changing code. The excerpt is a summary and can miss the real cause.

## Rules

1. Do not mark a task complete while `scripts/verify.sh --full` fails. Report the failure
   and what you tried, and stop.
2. Do not weaken a check to get green. That includes lowering a threshold, widening an
   allowlist, deleting or skipping a test or stage, and catching and ignoring errors. A
   failure that is not caused by your change goes to `scripts/verify/TRIAGE.md`, not into a
   silent exception.
3. Exceptions are allowed only in the three documented places, each with a reason, and a
   human reviews them:
   - secret findings: `scripts/verify/gitleaks.toml` (path allowlist, reason required);
   - dependency advisories: `scripts/verify/audit-exceptions.txt` (ID, expiry date, reason;
     an expired entry fails);
   - known lint scope: the scope lists in `scripts/verify/stages/lint.sh`.
4. Do not commit or push unless asked. Do not edit vendored trees (`third-party/`, `ffmpeg/`,
   `SDL2-*`, `sdl2-compat-*`, `switch/borealis/`) to fix an advisory; record it in TRIAGE.
5. Do not run the GUI against the user's real profile, and do not add `--profile` or
   `--opengl` to a smoke test. See the smoke stage for why.
6. Never print secret values. gitleaks runs with `--redact`; keep it that way.

## What each pillar covers here

| Pillar | Where | Notes |
| --- | --- | --- |
| Environment and config | `env` | No `.env` or config schema exists, so there are no required config keys to check. |
| Static analysis and types | `lint`, `build-core` | No formatter config exists. The compiler is the type checker, and `build-core` builds with warnings as errors. |
| Clean production build | `build-release` | Delegates to `scripts/translator/build-macos.sh` with `BUILD_DIR=build-release`. |
| Deterministic tests | `test-core`, `test-unit` | `TMPDIR` is per run and removed on exit. The tests use loopback sockets and `.invalid` URLs. Nothing enforces "no network". |
| Runtime smoke | `smoke` | The OCR and dictionary probes check real outputs. `--help` on the app checks that it starts and touches no user state. The windowed boot is not covered (see TRIAGE). Health endpoints and migrations do not apply to a desktop client. |
| Security and hygiene | `security` | Credential-shaped file names, gitleaks, and an osv-scanner audit that fails on HIGH and CRITICAL. Unrated advisories are counted, not failed. |

## Prerequisites

macOS with Homebrew, or Linux with the libraries installed (pkg-config resolves them, so any
distro works). Platform differences live in `scripts/verify/platform.sh` — adding a platform
means editing that file and nothing else.

Prerequisites are resolved per stage: `--quick` needs a compiler and SQLite, the security
scanners are only required by `security`, and the Qt stack only by `build-release` and
`test-unit`. `env` gates every stage and prints the missing items with an install command.
Harness-only Python packages are pinned in `scripts/verify/requirements.txt` and installed
into `build-verify/venv/`. Each run records exact tool versions in
`.verify-logs/<run-id>/tool-versions.txt`.

## Robustness

- Every stage has a time budget and is terminated if it exceeds it (reported as exit 124).
  Override with `VERIFY_TIMEOUT_<STAGE>` in seconds, e.g. `VERIFY_TIMEOUT_BUILD_RELEASE=7200`.
- A run holds a lock in `.verify-logs/lock`. A second run exits 2 rather than corrupting the
  shared build trees; a lock whose owner is gone is cleared automatically.
- `TRANS_ONNX=OFF` builds without the OCR backend. The OCR smoke probe then reports itself as
  not covered instead of failing, because the probe binary is not built in that configuration.

## Interrupts and cleanup

Each stage runs in its own process group. On `INT` or `TERM`, the harness kills every group,
removes its temp directory, and records the running stage as `INTERRUPTED`. The summary
reports the number of orphaned process groups, and it should be `0`.
