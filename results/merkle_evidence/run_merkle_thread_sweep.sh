#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
cd "${REPO_ROOT}"

BUILD_DIR="${BUILD_DIR:-build}"
BENCH_BIN="${BUILD_DIR}/bench_time"
THREADS_LIST=(1 2 4 8)

if [[ ! -x "${BENCH_BIN}" ]]; then
  echo "bench_time not found at ${BENCH_BIN}" >&2
  echo "hint: cmake -S . -B ${BUILD_DIR} && cmake --build ${BUILD_DIR} -j --target bench_time" >&2
  exit 1
fi

mkdir -p results/merkle_evidence/raw

run_case() {
  local workload="$1"
  local p="$2"
  local k_exp="$3"
  local r="$4"
  local n="$5"
  local d="$6"
  local stop_degree="$7"
  local ood_samples="$8"

  for t in "${THREADS_LIST[@]}"; do
    local out="results/merkle_evidence/raw/${workload}_t${t}.csv"
    echo "[${workload}] OMP_NUM_THREADS=${t} --threads ${t} -> ${out}" >&2
    OMP_NUM_THREADS="${t}" "${BENCH_BIN}" \
      --protocol stir9to3 \
      --p "${p}" --k-exp "${k_exp}" --r "${r}" \
      --n "${n}" --d "${d}" \
      --stop-degree "${stop_degree}" --ood-samples "${ood_samples}" \
      --threads "${t}" --warmup 1 --reps 1 --format csv > "${out}"
  done
}

run_case small 2 16 54 81 27 3 2
run_case large 2 16 486 729 243 3 2

python3 - <<'PY'
import csv, glob, os
rows = []
for path in sorted(glob.glob('results/merkle_evidence/raw/*_t*.csv')):
    name = os.path.basename(path)
    workload = name.split('_t')[0]
    threads = int(name.split('_t')[1].split('.')[0])
    with open(path, newline='') as f:
        r = next(csv.DictReader(f))
    rows.append({
        'workload': workload,
        'threads': threads,
        'ring': r['ring'],
        'n': int(r['n']),
        'd': int(r['d']),
        'prover_merkle_mean_ms': float(r['profile_prover_merkle_mean_ms']),
        'verify_merkle_mean_ms': float(r['profile_verify_merkle_mean_ms']),
        'prover_total_ms': float(r['prover_total_ms']),
        'verify_ms': float(r['verify_ms']),
    })
rows.sort(key=lambda x: (x['workload'], x['threads']))
base = {}
for r in rows:
    base.setdefault(r['workload'], r)

out = 'results/merkle_evidence/merkle_thread_scaling_summary.csv'
with open(out, 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow([
        'workload', 'ring', 'n', 'd', 'threads',
        'prover_merkle_mean_ms', 'verify_merkle_mean_ms',
        'prover_total_ms', 'verify_ms',
        'prover_merkle_speedup_vs_t1', 'verify_merkle_speedup_vs_t1',
    ])
    for r in rows:
        b = base[r['workload']]
        w.writerow([
            r['workload'], r['ring'], r['n'], r['d'], r['threads'],
            f"{r['prover_merkle_mean_ms']:.3f}",
            f"{r['verify_merkle_mean_ms']:.3f}",
            f"{r['prover_total_ms']:.3f}",
            f"{r['verify_ms']:.3f}",
            f"{(b['prover_merkle_mean_ms']/r['prover_merkle_mean_ms'] if r['prover_merkle_mean_ms'] else 0):.3f}",
            f"{(b['verify_merkle_mean_ms']/r['verify_merkle_mean_ms'] if r['verify_merkle_mean_ms'] else 0):.3f}",
        ])
print(out)
PY

echo "done: results/merkle_evidence/merkle_thread_scaling_summary.csv" >&2
