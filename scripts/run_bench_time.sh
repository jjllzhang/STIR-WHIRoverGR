#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

PRESET_PATH="${REPO_ROOT}/bench/presets/jia_micro_gr216_r162.json"
BUILD_DIR="${REPO_ROOT}/build"
OUTPUT_PATH=""
FORMAT="csv"
PROTOCOL_OVERRIDE=""
SEC_MODE="ConjectureCapacity"
HASH_PROFILE="STIR_NATIVE"
STOP_DEGREE="9"
OOD_SAMPLES="2"
QUERIES=""
THREADS=""
WARMUP="1"
REPS="3"
LAMBDA_TARGET=""
POW_BITS=""
P_VALUE=""
K_EXP=""
R_VALUE=""
N_VALUE=""
D_VALUE=""

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options]

Run Phase 6 time bench from a preset JSON plus optional overrides.

Options:
  --preset PATH         Preset JSON path (default: ${PRESET_PATH})
  --build-dir PATH      CMake build directory (default: ${BUILD_DIR})
  --output PATH         Output file path (default: results/time_<timestamp>.csv)
  --format FORMAT       csv | json (default: csv)
  --protocol VALUE      Override protocols: fri3 | fri9 | stir9to3 | all | comma,list
  --p VALUE             Override ring base prime p
  --k-exp VALUE         Override ring exponent k
  --r VALUE             Override ring extension degree r
  --n VALUE             Override domain size n
  --d VALUE             Override claimed degree d
  --lambda VALUE        Override lambda target
  --pow-bits VALUE      Override PoW bits
  --sec-mode VALUE      Override security mode (default: ${SEC_MODE})
  --hash-profile VALUE  Override hash profile (default: ${HASH_PROFILE})
  --stop-degree VALUE   Override stop degree (default: ${STOP_DEGREE})
  --ood-samples VALUE   Override OOD sample count (default: ${OOD_SAMPLES})
  --queries VALUE       Override query schedule string (default: auto; accepts auto or q0[,q1,...])
  --threads VALUE       Override threads
  --warmup VALUE        Override warmup iterations (default: ${WARMUP})
  --reps VALUE          Override measured repetitions (default: ${REPS})
  -h, --help            Show this help message
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --preset)
      PRESET_PATH="$2"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --output)
      OUTPUT_PATH="$2"
      shift 2
      ;;
    --format)
      FORMAT="$2"
      shift 2
      ;;
    --protocol)
      PROTOCOL_OVERRIDE="$2"
      shift 2
      ;;
    --p)
      P_VALUE="$2"
      shift 2
      ;;
    --k-exp)
      K_EXP="$2"
      shift 2
      ;;
    --r)
      R_VALUE="$2"
      shift 2
      ;;
    --n)
      N_VALUE="$2"
      shift 2
      ;;
    --d)
      D_VALUE="$2"
      shift 2
      ;;
    --lambda)
      LAMBDA_TARGET="$2"
      shift 2
      ;;
    --pow-bits)
      POW_BITS="$2"
      shift 2
      ;;
    --sec-mode)
      SEC_MODE="$2"
      shift 2
      ;;
    --hash-profile)
      HASH_PROFILE="$2"
      shift 2
      ;;
    --stop-degree)
      STOP_DEGREE="$2"
      shift 2
      ;;
    --ood-samples)
      OOD_SAMPLES="$2"
      shift 2
      ;;
    --queries)
      QUERIES="$2"
      shift 2
      ;;
    --threads)
      THREADS="$2"
      shift 2
      ;;
    --warmup)
      WARMUP="$2"
      shift 2
      ;;
    --reps)
      REPS="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ ! -f "${PRESET_PATH}" ]]; then
  echo "Preset not found: ${PRESET_PATH}" >&2
  exit 1
fi

BENCH_BIN="${BUILD_DIR}/bench_time"
if [[ ! -x "${BENCH_BIN}" ]]; then
  echo "Bench binary not found or not executable: ${BENCH_BIN}" >&2
  echo "Hint: cmake -S . -B build && cmake --build build -j" >&2
  exit 1
fi

if [[ "${FORMAT}" != "csv" && "${FORMAT}" != "json" ]]; then
  echo "time bench format must be csv or json, got: ${FORMAT}" >&2
  exit 1
fi

if [[ -z "${OUTPUT_PATH}" ]]; then
  mkdir -p "${REPO_ROOT}/results"
  TIMESTAMP=$(date +%Y%m%d_%H%M%S)
  OUTPUT_PATH="${REPO_ROOT}/results/time_${TIMESTAMP}.${FORMAT}"
else
  mkdir -p "$(dirname -- "${OUTPUT_PATH}")"
fi

mapfile -t PRESET_KV < <(python3 - "${PRESET_PATH}" <<'PY'
import json
import pathlib
import re
import sys

preset_path = pathlib.Path(sys.argv[1])
data = json.loads(preset_path.read_text())
ring = data.get("ring", "")
match = re.fullmatch(r"GR\((\d+)\^(\d+),(\d+)\)", ring)
if not match:
    raise SystemExit(f"Unsupported ring format in {preset_path}: {ring}")
protocols = data.get("protocols", ["fri3", "fri9", "stir9to3"])
print(f"PRESET_P={match.group(1)}")
print(f"PRESET_K_EXP={match.group(2)}")
print(f"PRESET_R={match.group(3)}")
print(f"PRESET_N={data['n']}")
print(f"PRESET_D={data['d']}")
print(f"PRESET_LAMBDA={data.get('lambda_target', 128)}")
print(f"PRESET_POW={data.get('pow_bits', data.get('pow_bits_time', 0))}")
print(f"PRESET_THREADS={data.get('threads', 1)}")
queries = data.get("queries")
if isinstance(queries, list):
    queries = ",".join(str(item) for item in queries)
elif queries is None:
    queries = ""
else:
    queries = str(queries)
print(f"PRESET_QUERIES={queries}")
print(f"PRESET_PROTOCOLS={','.join(protocols)}")
PY
)
for kv in "${PRESET_KV[@]}"; do
  eval "$kv"
done

P_VALUE=${P_VALUE:-$PRESET_P}
K_EXP=${K_EXP:-$PRESET_K_EXP}
R_VALUE=${R_VALUE:-$PRESET_R}
N_VALUE=${N_VALUE:-$PRESET_N}
D_VALUE=${D_VALUE:-$PRESET_D}
LAMBDA_TARGET=${LAMBDA_TARGET:-$PRESET_LAMBDA}
POW_BITS=${POW_BITS:-$PRESET_POW}
THREADS=${THREADS:-$PRESET_THREADS}
QUERIES=${QUERIES:-$PRESET_QUERIES}
PROTOCOLS=${PROTOCOL_OVERRIDE:-$PRESET_PROTOCOLS}

if [[ "${PROTOCOLS}" == "all" ]]; then
  PROTOCOLS="fri3,fri9,stir9to3"
fi
IFS=',' read -r -a PROTOCOL_LIST <<< "${PROTOCOLS}"

ARGS_COMMON=(
  --p "${P_VALUE}"
  --k-exp "${K_EXP}"
  --r "${R_VALUE}"
  --n "${N_VALUE}"
  --d "${D_VALUE}"
  --lambda "${LAMBDA_TARGET}"
  --pow-bits "${POW_BITS}"
  --sec-mode "${SEC_MODE}"
  --hash-profile "${HASH_PROFILE}"
  --stop-degree "${STOP_DEGREE}"
  --ood-samples "${OOD_SAMPLES}"
  --threads "${THREADS}"
  --warmup "${WARMUP}"
  --reps "${REPS}"
  --format "${FORMAT}"
)

if [[ -n "${QUERIES}" ]]; then
  ARGS_COMMON+=(--queries "${QUERIES}")
fi

CMD=("${BENCH_BIN}" --protocol "${PROTOCOLS}" "${ARGS_COMMON[@]}")
echo "[time] OMP_NUM_THREADS=${THREADS} OMP_DYNAMIC=false ${CMD[*]}" >&2
OMP_NUM_THREADS="${THREADS}" OMP_DYNAMIC=false "${CMD[@]}" > "${OUTPUT_PATH}"

echo "[time] wrote ${OUTPUT_PATH}" >&2
printf '%s\n' "${OUTPUT_PATH}"
