#!/usr/bin/env bash

set -euo pipefail

usage() {
    echo "Usage: $0 [--check] [PATH ...]"
    echo
    echo "Formats EKA2L1 source files with clang-format."
    echo "When PATH is omitted, tracked and untracked non-ignored project source files are used."
    echo "Vendored source under src/external is skipped."
}

MODE="format"
if [[ "${1:-}" == "--check" ]]; then
    MODE="check"
    shift
elif [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    usage
    exit 0
fi

ROOT="$(git rev-parse --show-toplevel 2>/dev/null || pwd)"

FMT=""
for CANDIDATE in "${CLANG_FORMAT:-}" clang-format clang-format-22 clang-format-21 clang-format-20 clang-format-19 clang-format-18 clang-format-17; do
    if [[ -n "${CANDIDATE}" ]] && command -v "${CANDIDATE}" >/dev/null 2>&1; then
        FMT="$(command -v "${CANDIDATE}")"
        break
    fi
done

if [[ -z "${FMT}" ]]; then
    echo "failed to find clang-format" >&2
    exit 1
fi

FILES=()
while IFS= read -r FILE; do
    [[ -n "${FILE}" ]] || continue

    case "${FILE}" in
        src/external/*)
            continue
            ;;
    esac

    [[ -f "${ROOT}/${FILE}" ]] || continue

    case "${FILE}" in
        *.c|*.cc|*.cpp|*.cxx|*.h|*.hh|*.hpp|*.hxx|*.m|*.mm)
            FILES+=("${ROOT}/${FILE}")
            ;;
    esac
done < <(git -C "${ROOT}" ls-files --cached --others --exclude-standard -- "$@")

if [[ ${#FILES[@]} -eq 0 ]]; then
    echo "No tracked source files matched."
    exit 0
fi

ARGS=(-i)
if [[ "${MODE}" == "check" ]]; then
    ARGS=(--dry-run --Werror)
fi

FAILED=0
for FILE in "${FILES[@]}"; do
    if ! "${FMT}" "${ARGS[@]}" "${FILE}"; then
        FAILED=1
    fi
done

if [[ ${FAILED} -ne 0 ]]; then
    echo "clang-format ${MODE} failed. Run '$0' to apply formatting." >&2
    exit 1
fi

echo "clang-format ${MODE} completed for ${#FILES[@]} source file(s)."
