#!/usr/bin/env bash
set -euo pipefail

# run_all_tests.sh - run requirements, code tests, and integration tests
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

echo "Running requirements check"
tests/requirements.sh

echo "Running code tests"
tests/code_tests.sh

echo "Running integration tests"
tests/integration.sh

echo "All tests completed"
exit 0
