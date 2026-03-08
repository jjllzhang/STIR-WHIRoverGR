#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

PRESET_PATH="${REPO_ROOT}/bench/presets/main_benchmark_workload_for_timing_gr216_r162.json"
BUILD_DIR="${REPO_ROOT}/build"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
COMBINED_CSV="${REPO_ROOT}/results/search_combined_${TIMESTAMP}.csv"
SUMMARY_MD="${REPO_ROOT}/results/search_summary_${TIMESTAMP}.md"

usage() {
  cat <<USAGE
Usage: $(basename "$0") [search options]

Wrapper around scripts/search_benchmark_parameters.py with practical defaults.

Default injected options:
  --preset ${PRESET_PATH}
  --build-dir ${BUILD_DIR}
  --combined-csv ${COMBINED_CSV}
  --summary-md ${SUMMARY_MD}

Any additional flags are passed through to search_benchmark_parameters.py.
Examples:
  $(basename "$0") --n-values 81,243 --rho-values 1/3,1/9 --soundness 128:22:ConjectureCapacity:auto
  $(basename "$0") --include-time --time-top-k 20 --time-metric prover_total_ms
  $(basename "$0") --combined-csv results/custom.csv --summary-md results/custom.md
USAGE
}

for arg in "$@"; do
  if [[ "$arg" == "-h" || "$arg" == "--help" ]]; then
    usage
    exec python3 "${SCRIPT_DIR}/search_benchmark_parameters.py" --help
  fi
done

mkdir -p "${REPO_ROOT}/results"

CMD=(
  python3 "${SCRIPT_DIR}/search_benchmark_parameters.py"
  --preset "${PRESET_PATH}"
  --build-dir "${BUILD_DIR}"
  --combined-csv "${COMBINED_CSV}"
  --summary-md "${SUMMARY_MD}"
)

CMD+=("$@")

echo "[search] ${CMD[*]}" >&2
"${CMD[@]}"
