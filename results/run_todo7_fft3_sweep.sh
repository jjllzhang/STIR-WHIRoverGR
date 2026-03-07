#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "${REPO_ROOT}"

BUILD_DIR="${BUILD_DIR:-build-release}"
BENCH_BIN="${BUILD_DIR}/bench_fft3"

if [[ ! -x "${BENCH_BIN}" ]]; then
  echo "bench_fft3 not found at ${BENCH_BIN}" >&2
  echo "hint: cmake -S . -B ${BUILD_DIR} && cmake --build ${BUILD_DIR} -j --target bench_fft3" >&2
  exit 1
fi

mkdir -p results

run_case() {
  local threads="$1"
  local mode="$2"
  local p="$3"
  local k_exp="$4"
  local r="$5"
  local n="$6"
  local d="$7"
  local output="$8"

  echo "[todo7] OMP_NUM_THREADS=${threads} --mode ${mode} --r ${r} --n ${n} --d ${d} -> ${output}" >&2
  OMP_NUM_THREADS="${threads}" OMP_DYNAMIC=false "${BENCH_BIN}" \
    --mode "${mode}" \
    --p "${p}" --k-exp "${k_exp}" --r "${r}" \
    --n "${n}" --d "${d}" \
    --warmup 1 --reps 5 --format csv > "${output}"
}

run_case 1 encode 2 16 162 243 81 results/todo7_fft3_encode_n243_t1.csv
run_case 8 encode 2 16 162 243 81 results/todo7_fft3_encode_n243_t8.csv
run_case 1 interpolate 2 16 162 243 81 results/todo7_fft3_interpolate_n243_t1.csv
run_case 8 interpolate 2 16 162 243 81 results/todo7_fft3_interpolate_n243_t8.csv

run_case 1 encode 2 16 486 729 243 results/todo7_fft3_encode_n729_t1.csv
run_case 8 encode 2 16 486 729 243 results/todo7_fft3_encode_n729_t8.csv
run_case 1 interpolate 2 16 486 729 243 results/todo7_fft3_interpolate_n729_t1.csv
run_case 8 interpolate 2 16 486 729 243 results/todo7_fft3_interpolate_n729_t8.csv

python3 - <<'PY'
import csv
from pathlib import Path

cases = [
    ("encode_n243_t1", Path("results/todo7_fft3_encode_n243_t1.csv")),
    ("encode_n243_t8", Path("results/todo7_fft3_encode_n243_t8.csv")),
    ("interpolate_n243_t1", Path("results/todo7_fft3_interpolate_n243_t1.csv")),
    ("interpolate_n243_t8", Path("results/todo7_fft3_interpolate_n243_t8.csv")),
    ("encode_n729_t1", Path("results/todo7_fft3_encode_n729_t1.csv")),
    ("encode_n729_t8", Path("results/todo7_fft3_encode_n729_t8.csv")),
    ("interpolate_n729_t1", Path("results/todo7_fft3_interpolate_n729_t1.csv")),
    ("interpolate_n729_t8", Path("results/todo7_fft3_interpolate_n729_t8.csv")),
]

rows = []
base_mean = {}
for case_name, path in cases:
    with path.open(newline="") as f:
        row = next(csv.DictReader(f))
    prefix = case_name.rsplit("_t", 1)[0]
    mean_ms = float(row["mean_ms"])
    base_mean.setdefault(prefix, mean_ms)
    rows.append({
        "case": case_name,
        "mode": row["mode"],
        "ring": row["ring"],
        "n": row["n"],
        "d": row["d"],
        "input_size": row["input_size"],
        "threads": case_name.split("_t", 1)[1],
        "warmup": row["warmup"],
        "reps": row["reps"],
        "mean_ms": f"{mean_ms:.3f}",
        "checksum": row["checksum"],
        "speedup_vs_t1": f"{base_mean[prefix] / mean_ms:.3f}",
    })

out = Path("results/todo7_fft3_summary.csv")
with out.open("w", newline="") as f:
    writer = csv.DictWriter(
        f,
        fieldnames=[
            "case",
            "mode",
            "ring",
            "n",
            "d",
            "input_size",
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

echo "done: results/todo7_fft3_summary.csv" >&2
