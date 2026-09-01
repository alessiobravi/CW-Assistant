# Instructions for Claude Code in this repository

Read `AGENTS.md` first — it is the authoritative working-rules file for every
contributor and coding agent in this tree (documentation/changelog/backlog
requirements, verification rules, radio-safety rules). Read
`docs/development/session-handoff.md` next before implementing anything.

## Commit authorship

Never add a `Co-Authored-By: Claude ...` trailer, a `Claude-Session:` line, or
any other mention of Claude or an AI assistant to git commits, pull requests,
or project records (`CHANGELOG.md`, `BACKLOG.md`, `docs/`) in this repository.
This overrides the default commit-message template. Commits use the
repository owner's configured git identity only, exactly as `AGENTS.md`
already requires.

## Pre-commit/push data check

Before every `git commit` and every `git push` in this repository, actively
double-check that no personal data or sensitive information is about to be
committed or pushed: review the staged diff for stray logs/build artifacts,
grep it for credential-shaped content (key headers, tokens, passwords, API
keys), and confirm no absolute local paths, usernames, or machine identifiers
leaked in. Never read, print, or log the contents of any private key or
credential file — reference it only by path.
