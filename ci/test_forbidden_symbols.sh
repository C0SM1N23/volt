#!/usr/bin/env bash
# Proves the D1 gate rejects a service that bypasses Environment.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "$test_root"' EXIT

mkdir -p "$test_root/services"
cp "$repo_root/ci/fixtures/forbidden_printf.txt" "$test_root/services/forbidden_printf.cpp"

if "$repo_root/ci/check_forbidden_symbols.sh" "$test_root/services" >/dev/null 2>&1; then
    echo "test_forbidden_symbols: checker accepted a service that calls printf" >&2
    exit 1
fi

echo "test_forbidden_symbols: OK"
