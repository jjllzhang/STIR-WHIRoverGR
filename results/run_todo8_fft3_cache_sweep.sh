#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "${REPO_ROOT}"

BUILD_DIR="${BUILD_DIR:-build-release}"
BENCH_FFT3_BIN="${BUILD_DIR}/bench_fft3"
BENCH_TIME_SCRIPT="${REPO_ROOT}/scripts/run_bench_time.sh"
CALLS_PER_REP="${CALLS_PER_REP:-8}"
FFT_WARMUP="${FFT_WARMUP:-1}"
FFT_REPS="${FFT_REPS:-5}"
TIME_WARMUP="${TIME_WARMUP:-1}"
TIME_REPS="${TIME_REPS:-3}"
TIME_PROTOCOLS="${TIME_PROTOCOLS:-fri9,stir9to3}"

if [[ ! -x "${BENCH_FFT3_BIN}" ]]; then
  echo "bench_fft3 not found at ${BENCH_FFT3_BIN}" >&2
  echo "hint: cmake -S . -B ${BUILD_DIR} && cmake --build ${BUILD_DIR} -j --target bench_fft3" >&2
  exit 1
fi

if [[ ! -x "${BUILD_DIR}/bench_time" ]]; then
  echo "bench_time not found at ${BUILD_DIR}/bench_time" >&2
  echo "hint: cmake -S . -B ${BUILD_DIR} && cmake --build ${BUILD_DIR} -j --target bench_time" >&2
  exit 1
fi

if [[ ! -x "${BENCH_TIME_SCRIPT}" ]]; then
  echo "run_bench_time.sh not found or not executable at ${BENCH_TIME_SCRIPT}" >&2
  exit 1
fi

mkdir -p results

run_fft_case() {
  local threads="$1"
  local mode="$2"
  local p="$3"
  local k_exp="$4"
  local r="$5"
  local n="$6"
  local d="$7"
  local output="$8"

  echo "[todo8][fft3] OMP_NUM_THREADS=${threads} --mode ${mode} --r ${r} --n ${n} --d ${d} --calls-per-rep ${CALLS_PER_REP} -> ${output}" >&2
  OMP_NUM_THREADS="${threads}" OMP_DYNAMIC=false "${BENCH_FFT3_BIN}" \
    --mode "${mode}" \
    --p "${p}" --k-exp "${k_exp}" --r "${r}" \
    --n "${n}" --d "${d}" \
    --warmup "${FFT_WARMUP}" --reps "${FFT_REPS}" \
    --calls-per-rep "${CALLS_PER_REP}" \
    --format csv > "${output}"
}

run_time_case() {
  local threads="$1"
  local output="$2"

  echo "[todo8][time] protocols=${TIME_PROTOCOLS} threads=${threads} -> ${output}" >&2
  "${BENCH_TIME_SCRIPT}" \
    --build-dir "${BUILD_DIR}" \
    --protocol "${TIME_PROTOCOLS}" \
    --threads "${threads}" \
    --warmup "${TIME_WARMUP}" \
    --reps "${TIME_REPS}" \
    --format csv \
    --output "${output}" >/dev/null
}

run_fft_case 1 encode 2 16 162 243 81 results/todo8_fft3_encode_n243_t1.csv
run_fft_case 8 encode 2 16 162 243 81 results/todo8_fft3_encode_n243_t8.csv
run_fft_case 1 interpolate 2 16 162 243 81 results/todo8_fft3_interpolate_n243_t1.csv
run_fft_case 8 interpolate 2 16 162 243 81 results/todo8_fft3_interpolate_n243_t8.csv

run_fft_case 1 encode 2 16 486 729 243 results/todo8_fft3_encode_n729_t1.csv
run_fft_case 8 encode 2 16 486 729 243 results/todo8_fft3_encode_n729_t8.csv
run_fft_case 1 interpolate 2 16 486 729 243 results/todo8_fft3_interpolate_n729_t1.csv
run_fft_case 8 interpolate 2 16 486 729 243 results/todo8_fft3_interpolate_n729_t8.csv

run_time_case 1 results/todo8_time_t1.csv
run_time_case 8 results/todo8_time_t8.csv

python3 - <<'PY'
import csv
from pathlib import Path

fft_cases = [
    ("encode_n243_t1", Path("results/todo8_fft3_encode_n243_t1.csv")),
    ("encode_n243_t8", Path("results/todo8_fft3_encode_n243_t8.csv")),
    ("interpolate_n243_t1", Path("results/todo8_fft3_interpolate_n243_t1.csv")),
    ("interpolate_n243_t8", Path("results/todo8_fft3_interpolate_n243_t8.csv")),
    ("encode_n729_t1", Path("results/todo8_fft3_encode_n729_t1.csv")),
    ("encode_n729_t8", Path("results/todo8_fft3_encode_n729_t8.csv")),
    ("interpolate_n729_t1", Path("results/todo8_fft3_interpolate_n729_t1.csv")),
    ("interpolate_n729_t8", Path("results/todo8_fft3_interpolate_n729_t8.csv")),
]

fft_rows = []
base_mean = {}
base_mean_call = {}
for case_name, path in fft_cases:
    with path.open(newline="") as f:
        row = next(csv.DictReader(f))
    prefix = case_name.rsplit("_t", 1)[0]
    mean_ms = float(row["mean_ms"])
    mean_call_ms = float(row["mean_call_ms"])
    base_mean.setdefault(prefix, mean_ms)
    base_mean_call.setdefault(prefix, mean_call_ms)
    fft_rows.append({
        "case": case_name,
        "mode": row["mode"],
        "ring": row["ring"],
        "n": row["n"],
        "d": row["d"],
        "input_size": row["input_size"],
        "threads": case_name.split("_t", 1)[1],
        "warmup": row["warmup"],
        "reps": row["reps"],
        "calls_per_rep": row["calls_per_rep"],
        "mean_ms": f"{mean_ms:.3f}",
        "mean_call_ms": f"{mean_call_ms:.3f}",
        "checksum": row["checksum"],
        "speedup_vs_t1": f"{base_mean[prefix] / mean_ms:.3f}",
        "call_speedup_vs_t1": f"{base_mean_call[prefix] / mean_call_ms:.3f}",
    })

fft_out = Path("results/todo8_fft3_cache_summary.csv")
with fft_out.open("w", newline="") as f:
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
            "calls_per_rep",
            "mean_ms",
            "mean_call_ms",
            "checksum",
            "speedup_vs_t1",
            "call_speedup_vs_t1",
        ],
    )
    writer.writeheader()
    writer.writerows(fft_rows)

time_paths = {
    "1": Path("results/todo8_time_t1.csv"),
    "8": Path("results/todo8_time_t8.csv"),
}
time_rows = {}
for threads, path in time_paths.items():
    with path.open(newline="") as f:
      for row in csv.DictReader(f):
        time_rows[(row["protocol"], threads)] = row

time_out_rows = []
for protocol in sorted({protocol for protocol, _ in time_rows}):
    base = time_rows[(protocol, "1")]
    parallel = time_rows[(protocol, "8")]

    def as_float(row, key):
        return float(row[key])

    base_encode = as_float(base, "profile_prover_encode_mean_ms")
    base_interp = as_float(base, "profile_prover_interpolate_mean_ms")
    base_verify_alg = as_float(base, "profile_verify_algebra_mean_ms")
    base_prover_total = as_float(base, "profile_prover_total_mean_ms")
    base_verify = as_float(base, "verify_ms")

    par_encode = as_float(parallel, "profile_prover_encode_mean_ms")
    par_interp = as_float(parallel, "profile_prover_interpolate_mean_ms")
    par_verify_alg = as_float(parallel, "profile_verify_algebra_mean_ms")
    par_prover_total = as_float(parallel, "profile_prover_total_mean_ms")
    par_verify = as_float(parallel, "verify_ms")

    time_out_rows.append({
        "protocol": protocol,
        "ring": base["ring"],
        "n": base["n"],
        "d": base["d"],
        "threads_t1": "1",
        "threads_t8": "8",
        "prover_total_mean_ms_t1": f"{base_prover_total:.3f}",
        "prover_total_mean_ms_t8": f"{par_prover_total:.3f}",
        "prover_total_speedup": f"{base_prover_total / par_prover_total:.3f}",
        "encode_mean_ms_t1": f"{base_encode:.3f}",
        "encode_mean_ms_t8": f"{par_encode:.3f}",
        "encode_speedup": f"{base_encode / par_encode:.3f}",
        "interpolate_mean_ms_t1": f"{base_interp:.3f}",
        "interpolate_mean_ms_t8": f"{par_interp:.3f}",
        "interpolate_speedup": f"{base_interp / par_interp:.3f}",
        "verify_algebra_mean_ms_t1": f"{base_verify_alg:.3f}",
        "verify_algebra_mean_ms_t8": f"{par_verify_alg:.3f}",
        "verify_algebra_speedup": f"{base_verify_alg / par_verify_alg:.3f}",
        "verify_ms_t1": f"{base_verify:.3f}",
        "verify_ms_t8": f"{par_verify:.3f}",
        "verify_speedup": f"{base_verify / par_verify:.3f}",
    })

time_out = Path("results/todo8_time_summary.csv")
with time_out.open("w", newline="") as f:
    writer = csv.DictWriter(
        f,
        fieldnames=[
            "protocol",
            "ring",
            "n",
            "d",
            "threads_t1",
            "threads_t8",
            "prover_total_mean_ms_t1",
            "prover_total_mean_ms_t8",
            "prover_total_speedup",
            "encode_mean_ms_t1",
            "encode_mean_ms_t8",
            "encode_speedup",
            "interpolate_mean_ms_t1",
            "interpolate_mean_ms_t8",
            "interpolate_speedup",
            "verify_algebra_mean_ms_t1",
            "verify_algebra_mean_ms_t8",
            "verify_algebra_speedup",
            "verify_ms_t1",
            "verify_ms_t8",
            "verify_speedup",
        ],
    )
    writer.writeheader()
    writer.writerows(time_out_rows)

print(fft_out)
print(time_out)
PY

echo "done: results/todo8_fft3_cache_summary.csv" >&2
echo "done: results/todo8_time_t1.csv" >&2
echo "done: results/todo8_time_t8.csv" >&2
echo "done: results/todo8_time_summary.csv" >&2
