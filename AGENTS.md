# Repository working rules

These rules apply to every contributor and coding agent working in this tree.

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
- Do not weaken, skip, or conditionally bypass a platform failure merely to make
  CI green. Record genuine platform exclusions in the requirements and backlog.

## Radio safety

- Decoder output never directly controls PTT or KEY.
- Hardware transmission work must preserve explicit arming, exact callsign
  confirmation, maximum-key-down timeout, emergency release, and safe inactive
  serial-line initialization.
- Tests use mocks or physical loopback until the documented reference-rig test
  procedure explicitly authorizes on-air operation.
