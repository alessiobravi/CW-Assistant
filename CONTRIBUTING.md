# Contributing

## Change workflow

1. Select or add a backlog ID in `BACKLOG.md` and mark it `active` when work
   begins.
2. Keep core logic independent of UI and hardware libraries.
3. Add deterministic tests and run the relevant local test suite.
4. Add the result under `CHANGELOG.md` → `Unreleased`.
5. Update the backlog item status, scope, or review note.
6. Open a pull request and let every cross-platform CI job pass.

Pull requests that alter source, tests, build files, or workflows must include
both `CHANGELOG.md` and `BACKLOG.md`. The `project-records` CI job enforces this.

## Commit and pull-request scope

Prefer one coherent backlog item per pull request. Mention its ID in the pull
request description. Keep generated builds, captured radio data, private keys,
and vendor SDK binaries out of Git.

## Testing levels

- Unit: dependency-free algorithm and state tests.
- Golden vector: DSP input/output with numerical tolerances.
- Replay: WAV/SigMF corpus with stable metrics.
- Adapter contract: mocks and serial/network loopback.
- Hardware acceptance: explicitly named devices and a written safe procedure.
- Soak/performance: bounded queues, latency, overrun counters, and UI response.
