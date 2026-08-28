#!/usr/bin/env bash
# Scans VOLT source and header files for patterns banned outright by
# AGENTS.md section 2. Any match fails the check.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

source_dirs=(apps platform distributed communication diagnostics safety security services simulation firmware tools tests)
existing_dirs=()
for dir in "${source_dirs[@]}"; do
    [[ -d "$dir" ]] && existing_dirs+=("$dir")
done

mapfile -d '' -t files < <(
    find "${existing_dirs[@]}" -type f \
        \( -name '*.h' -o -name '*.hpp' -o -name '*.hxx' -o -name '*.hh' \
           -o -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
           -o -name '*.tpp' -o -name '*.ipp' \) \
        -print0 2>/dev/null
)

failed=0

check() {
    local description="$1" pattern="$2"
    local matches
    if [[ "${#files[@]}" -eq 0 ]]; then
        return
    fi
    matches="$(grep -HnE "$pattern" -- "${files[@]}" 2>/dev/null || true)"
    if [[ -n "$matches" ]]; then
        echo "BANNED: $description"
        echo "$matches"
        echo
        failed=1
    fi
}

check_i() {
    local description="$1" pattern="$2"
    local matches
    if [[ "${#files[@]}" -eq 0 ]]; then
        return
    fi
    matches="$(grep -HinE "$pattern" -- "${files[@]}" 2>/dev/null || true)"
    if [[ -n "$matches" ]]; then
        echo "BANNED: $description"
        echo "$matches"
        echo
        failed=1
    fi
}

check "TODO/FIXME/XXX/HACK markers"                       '\b(TODO|FIXME|XXX|HACK)\b'
check_i "'for now' / 'temporary' / 'will be implemented later' phrasing" '(for now|temporary|will be implemented later)'
check "std::cout / std::endl usage"                       '\bstd::(cout|endl)\b'
check "raw <iostream> include"                            '#include[[:space:]]*<iostream>'
check "printf() usage"                                    '\bprintf[[:space:]]*\('
check "new/delete used directly"                          '\b(new|delete)\b'
check "banned libc functions (rand/srand/strcpy/strcat/sprintf/gets/atoi)" \
                                                            '\b(rand|srand|strcpy|strcat|sprintf|gets|atoi)[[:space:]]*\('
check "commented-out code (// line containing ; or {)"     '^[[:space:]]*//.*[;{]'

# #define outside the macro allow-list (VOLT_TRY, VOLT_ASSERT, VOLT_LOG_*,
# VOLT_TRACE, VOLT_CHECKPOINT, VOLT_LOOP_BOUND).
allowed_macro_regex='^(VOLT_TRY|VOLT_ASSERT|VOLT_LOG_[A-Za-z0-9_]*|VOLT_TRACE|VOLT_CHECKPOINT|VOLT_LOOP_BOUND)$'
define_matches=""
if [[ "${#files[@]}" -gt 0 ]]; then
    for f in "${files[@]}"; do
        while IFS=: read -r lineno content; do
            [[ -z "${lineno:-}" ]] && continue
            name="$(sed -E 's/^[[:space:]]*#define[[:space:]]+([A-Za-z_][A-Za-z0-9_]*).*/\1/' <<<"$content")"
            if ! [[ "$name" =~ $allowed_macro_regex ]]; then
                define_matches+="${f}:${lineno}:${content}"$'\n'
            fi
        done < <(grep -noE '^[[:space:]]*#define[[:space:]]+[A-Za-z_][A-Za-z0-9_]*.*' "$f" 2>/dev/null || true)
    done
fi
if [[ -n "$define_matches" ]]; then
    echo "BANNED: #define outside the allowed macro list (VOLT_TRY, VOLT_ASSERT, VOLT_LOG_*, VOLT_TRACE, VOLT_CHECKPOINT, VOLT_LOOP_BOUND)"
    echo -n "$define_matches"
    echo
    failed=1
fi

if [[ "$failed" -ne 0 ]]; then
    echo "check_banned_patterns: FAILED"
    exit 1
fi

echo "check_banned_patterns: OK (${#files[@]} files scanned)"
