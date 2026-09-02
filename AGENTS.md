# Repository working rules

These rules apply to every contributor and coding agent working in this tree.

## Canonical Git coordinates

- The canonical repository is `git@github.com:alessiobravi/CW-Assistant.git`
  (web URL: `https://github.com/alessiobravi/CW-Assistant`) and its default
  development branch is `main`.
- Use this tree's own `.git` metadata when present. If the environment cannot
  create a `.git` entry and the recovered `.cwa-git` store is present, run Git
  as `git --git-dir=.cwa-git --work-tree=.` from the repository root.
- Before fetching, committing, tagging, or pushing, verify that
  `remote.origin.url` is the canonical SSH URL above. Never borrow, move, or
  modify Git metadata from a neighboring project to operate on this tree.
- Keep the recovered `.cwa-git` store local and untracked. It must contain
  `core.worktree=..`, the canonical `origin`, and the repository owner's Git
  identity so normal fetch/commit/push operations remain available.

Before starting or resuming implementation, read
`docs/development/session-handoff.md` for the current architecture, decoder
state, validation commands, and ordered continuation checklist. Source, tests,
this file, and the current backlog remain authoritative if the handoff is stale.

## Required project records

- Add every user-visible, architectural, build, dependency, security, or notable
  internal change to the `Unreleased` section of `CHANGELOG.md`.
- Review `BACKLOG.md` for every change. Update affected item status or scope. If
  no item changes, update its `Last reviewed` note with the date and reason.
- A change to source, tests, CMake, or GitHub workflows is incomplete unless
  both records are included in the same commit or pull request.
- Every developed user-facing feature, setting, workflow, platform/package, or
  behavior must update the relevant readable guide under `docs/manuals/` with
  setup steps, limitations, and practical examples. Keep engineering documents
  under `docs/` consistent when architecture or requirements change.
- Use the repository owner's configured Git identity for commits. Do not add
  Codex, an AI assistant, or another automated system as author or co-author,
  and do not add automated `Co-authored-by` trailers.

## Verification

- Keep the dependency-free core warning-clean under MSVC, Clang, and GCC.
- Add or update deterministic tests with behavior changes.
- Run the relevant local build/tests and allow the complete GitHub Actions
  matrix to pass before merging.
- After every implementation push, monitor every applicable GitHub Actions job
  through completion. Treat only an all-green result as verified: inspect every
  failure or cancellation, correct it, push the fix, and repeat until all jobs
  pass. If Actions cannot be queried, report that verification gap explicitly
  and do not claim the work is fully verified.
- When a workflow run fails, keep the exact same implementation title on every
  corrective rerun and continue correcting/re-running it until all required
  jobs and the publication stage are green.
- Do not weaken, skip, or conditionally bypass a platform failure merely to make
  CI green. Record genuine platform exclusions in the requirements and backlog.

## Radio safety

- Decoder output never directly controls PTT or KEY.
- Hardware transmission work must preserve explicit arming, exact callsign
  confirmation, maximum-key-down timeout, emergency release, and safe inactive
  serial-line initialization.
- Tests use mocks or physical loopback until the documented reference-rig test
  procedure explicitly authorizes on-air operation.
