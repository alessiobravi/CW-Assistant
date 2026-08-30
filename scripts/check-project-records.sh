#!/usr/bin/env bash
set -euo pipefail

base_revision="${1:?base revision is required}"
head_revision="${2:?head revision is required}"

records_required=false
changelog_updated=false
backlog_updated=false
manual_updated=false

while IFS= read -r changed_path; do
  case "${changed_path}" in
    CHANGELOG.md) changelog_updated=true ;;
    BACKLOG.md) backlog_updated=true ;;
    docs/manuals/*) manual_updated=true ;;
    src/*|tests/*|cmake/*|CMakeLists.txt|CMakePresets.json|.github/workflows/*)
      records_required=true
      ;;
  esac
done < <(git diff --name-only "${base_revision}" "${head_revision}")

if [[ "${records_required}" == false ]]; then
  echo "No implementation/build/workflow change; project-record update is optional."
  exit 0
fi

missing=()
[[ "${changelog_updated}" == true ]] || missing+=("CHANGELOG.md")
[[ "${backlog_updated}" == true ]] || missing+=("BACKLOG.md")
[[ "${manual_updated}" == true ]] || missing+=("docs/manuals/")

if (( ${#missing[@]} > 0 )); then
  echo "Implementation changes require updated records and user manuals."
  printf 'Missing: %s\n' "${missing[@]}"
  exit 1
fi

echo "Changelog, backlog, and user manuals accompany this implementation change."
