#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "${REPO_ROOT}"

BUILD_DIR="${BUILD_DIR:-build-release}"
BENCH_BIN="${BUILD_DIR}/bench_fold_table"

if [[ ! -x "${BENCH_BIN}" ]]; then
  echo "bench_fold_table not found at ${BENCH_BIN}" >&2
  echo "hint: cmake -S . -B ${BUILD_DIR} && cmake --build ${BUILD_DIR} -j --target bench_fold_table" >&2
  exit 1
fi

mkdir -p results

run_case() {
  local threads="$1"
  local p="$2"
  local k_exp="$3"
  local r="$4"
  local n="$5"
  local fold="$6"
  local output="$7"

  echo "[todo6] OMP_NUM_THREADS=${threads} --r ${r} --n ${n} --fold ${fold} -> ${output}" >&2
  OMP_NUM_THREADS="${threads}" "${BENCH_BIN}" \
    --p "${p}" --k-exp "${k_exp}" --r "${r}" \
    --n "${n}" --fold "${fold}" \
    --warmup 1 --reps 5 --format csv > "${output}"
}

run_case 1 2 16 162 243 9 results/todo6_fold_n243_t1.csv
run_case 8 2 16 162 243 9 results/todo6_fold_n243_t8.csv
run_case 1 2 16 486 729 9 results/todo6_fold_n729_t1.csv
run_case 8 2 16 486 729 9 results/todo6_fold_n729_t8.csv

python3 - <<'PY'
import csv
from pathlib import Path

cases = [
    ("n243_t1", Path("results/todo6_fold_n243_t1.csv")),
    ("n243_t8", Path("results/todo6_fold_n243_t8.csv")),
    ("n729_t1", Path("results/todo6_fold_n729_t1.csv")),
    ("n729_t8", Path("results/todo6_fold_n729_t8.csv")),
]

rows = []
base_mean = {}
for case_name, path in cases:
    with path.open(newline="") as f:
      row = next(csv.DictReader(f))
    prefix = case_name.split("_t", 1)[0]
    mean_ms = float(row["mean_ms"])
    base_mean.setdefault(prefix, mean_ms)
    rows.append({
        "case": case_name,
        "ring": row["ring"],
        "n": row["n"],
        "folded_n": row["folded_n"],
        "fold": row["fold"],
        "threads": case_name.split("_t", 1)[1],
        "warmup": row["warmup"],
        "reps": row["reps"],
        "mean_ms": f"{mean_ms:.3f}",
        "checksum": row["checksum"],
        "speedup_vs_t1": f"{base_mean[prefix] / mean_ms:.3f}",
    })

out = Path("results/todo6_fold_summary.csv")
with out.open("w", newline="") as f:
    writer = csv.DictWriter(
        f,
        fieldnames=[
            "case",
            "ring",
            "n",
            "folded_n",
            "fold",
            "threads",
            "warmup",
            "reps",
            "mean_ms",
            "checksum",
            "speedup_vs_t1",
        ],
    )
    writer.writeheader()
    writer.writerows(rows)

print(out)
PY

echo "done: results/todo6_fold_summary.csv" >&2
