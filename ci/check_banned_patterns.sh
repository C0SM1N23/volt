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

# Same as check(), but with line comments blanked out first. Prose is allowed
# to contain the words these patterns look for; code is not.
check_code() {
    local description="$1" pattern="$2"
    local matches="" hit
    for f in "${files[@]}"; do
        hit="$(sed 's|//.*||' "$f" | grep -nE "$pattern" 2>/dev/null || true)"
        if [[ -n "$hit" ]]; then
            matches+="$(sed "s|^|${f}:|" <<<"$hit")"$'\n'
        fi
    done
    if [[ -n "$matches" ]]; then
        echo "BANNED: $description"
        echo -n "$matches"
        echo
        failed=1
    fi
}

check "TODO/FIXME/XXX/HACK markers"                       '\b(TODO|FIXME|XXX|HACK)\b'
check_i "'for now' / 'temporary' / 'will be implemented later' phrasing" '(for now|temporary|will be implemented later)'
check_code "std::cout / std::endl usage"                  '\bstd::(cout|endl)\b'
check "raw <iostream> include"                            '#include[[:space:]]*<iostream>'
check_code "printf() usage"                               '\bprintf[[:space:]]*\('
# Allocation, not the `= delete` that suppresses a special member function: a
# deleted copy constructor is how a polymorphic interface prevents slicing.
check_code "operator new used directly"                   '\bnew[[:space:]]+[A-Za-z_][A-Za-z0-9_:<>]*[[:space:]]*[][({;]|\bnew[[:space:]]*\('
check_code "operator delete used directly"                '\bdelete[[:space:]]+[A-Za-z_*(]|\bdelete[[:space:]]*\[[[:space:]]*\]'
check_code "malloc/free used directly"                    '\b(malloc|calloc|realloc|free)[[:space:]]*\('
check_code "banned libc functions (rand/srand/strcpy/strcat/sprintf/gets/atoi)" \
                                                            '\b(rand|srand|strcpy|strcat|sprintf|gets|atoi)[[:space:]]*\('
# Commented-out code is recognised by two things at once: the line ends the way
# a statement or a block does, and it is not a documentation comment. Matching a
# ';' anywhere would flag ordinary prose, since a semicolon is also punctuation,
# and code is not commented out with '///'.
check "commented-out code (// line ending in ; { or })"    '^[[:space:]]*//([^/].*)?[;{}][[:space:]]*$'

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
