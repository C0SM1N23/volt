#!/usr/bin/env bash
# Rejects symbols that must never reach the data plane. The patterns live in
# tools/forbidden_symbols.txt so that adding one does not mean touching this
# script. SPEC 7.3 restricts the scan to services/ and safety/.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

symbol_list="tools/forbidden_symbols.txt"
if [[ ! -f "$symbol_list" ]]; then
    echo "check_forbidden_symbols: missing symbol list at $symbol_list" >&2
    exit 1
fi

scan_dirs=(services safety)
existing_dirs=()
for dir in "${scan_dirs[@]}"; do
    [[ -d "$dir" ]] && existing_dirs+=("$dir")
done

files=()
if [[ "${#existing_dirs[@]}" -gt 0 ]]; then
    mapfile -d '' -t files < <(
        find "${existing_dirs[@]}" -type f \
            \( -name '*.h' -o -name '*.hpp' -o -name '*.hxx' -o -name '*.hh' \
               -o -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' \
               -o -name '*.tpp' -o -name '*.ipp' \) \
            -print0 2>/dev/null
    )
fi

patterns=()
while IFS= read -r line; do
    [[ -z "$line" || "$line" =~ ^[[:space:]]*# ]] && continue
    patterns+=("$line")
done < "$symbol_list"

failed=0
if [[ "${#patterns[@]}" -gt 0 && "${#files[@]}" -gt 0 ]]; then
    for pattern in "${patterns[@]}"; do
        matches="$(grep -HnE "$pattern" -- "${files[@]}" 2>/dev/null || true)"
        if [[ -n "$matches" ]]; then
            echo "FORBIDDEN SYMBOL: $pattern"
            echo "$matches"
            echo
            failed=1
        fi
    done
fi

if [[ "$failed" -ne 0 ]]; then
    echo "check_forbidden_symbols: FAILED"
    exit 1
fi

echo "check_forbidden_symbols: OK (${#patterns[@]} patterns, ${#files[@]} files scanned)"
