#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/../.." && pwd)

PRESET_PATH="${REPO_ROOT}/bench/presets/fri.json"
BUILD_DIR="${REPO_ROOT}/build"
OUTPUT_PATH=""
FORMAT="csv"
PROTOCOL_OVERRIDE=""
SEC_MODE=""
HASH_PROFILE=""
STOP_DEGREE=""
OOD_SAMPLES=""
QUERIES=""
THREADS=""
WARMUP="1"
REPS="3"
FRI_SOUNDNESS_MODE=""
LAMBDA_TARGET=""
POW_BITS=""
P_VALUE=""
K_EXP=""
R_VALUE=""
N_VALUE=""
D_VALUE=""
FRI_REPETITIONS=""
WHIR_M=""
WHIR_BMAX=""
WHIR_R=""
WHIR_RHO0=""
WHIR_POLYNOMIAL=""
WHIR_REPETITIONS=""
EXPERIMENT_FILTER=""

usage() {
  cat <<USAGE
Usage: $(basename "$0") [options]

Run a benchmark preset through bench_time plus optional overrides.

Options:
  --preset PATH         Preset JSON path (default: ${PRESET_PATH})
  --build-dir PATH      CMake build directory (default: ${BUILD_DIR})
  --output PATH         Output file path (default: results/runs/<timestamp>/<preset>_timing.<format>)
  --format FORMAT       csv | json (default: csv)
  --protocol VALUE      Override protocols: fri3 | fri9 | stir9to3 | whir_gr_ud | all | comma,list
  --p VALUE             Override ring base prime p
  --k-exp VALUE         Override ring exponent k
  --r VALUE             Override ring extension degree r
  --n VALUE             Override domain size n
  --d VALUE             Override claimed degree d
  --fri-soundness-mode VALUE
                        Override FRI soundness mode: theorem_auto | manual_repetition
  --fri-repetitions VALUE
                        Override FRI repetition count m
  --lambda VALUE        Override lambda target
  --pow-bits VALUE      Override PoW bits
  --sec-mode VALUE      Override security mode (default: preset value, else ConjectureCapacity)
  --hash-profile VALUE  Override hash profile (default: preset value, else STIR_NATIVE)
  --stop-degree VALUE   Override stop degree (default: preset value, else 9)
  --ood-samples VALUE   Override OOD sample count (default: preset value, else 2)
  --queries VALUE       Override query schedule string (default: auto; accepts auto or q0[,q1,...])
  --whir-m VALUE        Override WHIR variable count
  --whir-bmax VALUE     Override WHIR maximum layer width
  --whir-r VALUE        Override fixed WHIR extension degree
  --whir-fixed-r VALUE  Alias for --whir-r
  --whir-rho0 VALUE     Override WHIR initial rate, e.g. 1/3
  --whir-polynomial VALUE
                        Override WHIR polynomial family: multiquadratic | multilinear
  --whir-repetitions VALUE
                        Override WHIR shift/final repetitions for debugging
  --threads VALUE       Override threads
  --warmup VALUE        Override warmup iterations (default: ${WARMUP})
  --reps VALUE          Override measured repetitions (default: ${REPS})
  --experiments VALUE   Run only selected experiment names or 1-based indices/ranges
                        (comma list, e.g. 2,4-6 or gr216_r162_main)
  --experiment VALUE    Alias for --experiments
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
    --fri-soundness-mode)
      FRI_SOUNDNESS_MODE="$2"
      shift 2
      ;;
    --fri-repetitions)
      FRI_REPETITIONS="$2"
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
    --whir-m)
      WHIR_M="$2"
      shift 2
      ;;
    --whir-bmax)
      WHIR_BMAX="$2"
      shift 2
      ;;
    --whir-r|--whir-fixed-r)
      WHIR_R="$2"
      shift 2
      ;;
    --whir-rho0)
      WHIR_RHO0="$2"
      shift 2
      ;;
    --whir-polynomial)
      WHIR_POLYNOMIAL="$2"
      shift 2
      ;;
    --whir-repetitions)
      WHIR_REPETITIONS="$2"
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
    --experiments|--experiment)
      EXPERIMENT_FILTER="$2"
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
  echo "benchmark output format must be csv or json, got: ${FORMAT}" >&2
  exit 1
fi

if [[ -z "${OUTPUT_PATH}" ]]; then
  TIMESTAMP=$(date +%Y%m%d_%H%M%S)
  PRESET_STEM=$(basename -- "${PRESET_PATH%.*}")
  RUN_DIR="${REPO_ROOT}/results/runs/${TIMESTAMP}"
  mkdir -p "${RUN_DIR}"
  OUTPUT_PATH="${RUN_DIR}/${PRESET_STEM}_timing.${FORMAT}"
else
  mkdir -p "$(dirname -- "${OUTPUT_PATH}")"
fi

mapfile -t PRESET_EXPERIMENTS < <(python3 - "${PRESET_PATH}" "${EXPERIMENT_FILTER}" <<'PY'
import json
import pathlib
import re
import shlex
import sys

preset_path = pathlib.Path(sys.argv[1])
experiment_filter = sys.argv[2].strip()
data = json.loads(preset_path.read_text())

def merged_experiments(raw):
    if "experiments" not in raw:
        return [raw]
    defaults = raw.get("defaults", {})
    if not isinstance(defaults, dict):
        raise SystemExit(f"Preset defaults must be an object in {preset_path}")
    base = {
        key: value
        for key, value in raw.items()
        if key not in {"defaults", "experiments", "description", "notes"}
    }
    shared = {**base, **defaults}
    experiments = raw["experiments"]
    if not isinstance(experiments, list):
        raise SystemExit(f"Preset experiments must be a list in {preset_path}")
    out = []
    for index, experiment in enumerate(experiments, start=1):
        if not isinstance(experiment, dict):
            raise SystemExit(
                f"Preset experiment #{index} must be an object in {preset_path}"
            )
        merged = {**shared, **experiment}
        merged.setdefault("name", f"experiment_{index}")
        out.append(merged)
    return out

def parse_experiment_filter(raw, experiments):
    if not raw:
        return list(enumerate(experiments, start=1))
    selected = []
    seen = set()
    names = {str(experiment.get("name", f"experiment_{index}")): index
             for index, experiment in enumerate(experiments, start=1)}
    for token in [item.strip() for item in raw.split(",") if item.strip()]:
        indices = []
        if token in names:
            indices = [names[token]]
        elif re.fullmatch(r"[0-9]+", token):
            indices = [int(token)]
        elif re.fullmatch(r"[0-9]+-[0-9]+", token):
            begin, end = (int(part) for part in token.split("-", 1))
            if begin > end:
                raise SystemExit(f"Invalid descending experiment range: {token}")
            indices = list(range(begin, end + 1))
        else:
            available = ", ".join(
                f"{index}:{experiment.get('name', f'experiment_{index}')}"
                for index, experiment in enumerate(experiments, start=1)
            )
            raise SystemExit(
                f"Unknown experiment selector '{token}'. Available: {available}"
            )
        for index in indices:
            if index < 1 or index > len(experiments):
                raise SystemExit(
                    f"Experiment index {index} out of range 1..{len(experiments)}"
                )
            if index in seen:
                continue
            seen.add(index)
            selected.append((index, experiments[index - 1]))
    if not selected:
        raise SystemExit(f"Experiment filter selected no experiments: {raw}")
    return selected

def shell_assignments(run_index, run_total, preset_index, experiment):
    ring = experiment.get("ring", "")
    match = re.fullmatch(r"GR\((\d+)\^(\d+),(\d+)\)", ring)
    if not match:
        raise SystemExit(f"Unsupported ring format in {preset_path}: {ring}")
    protocols = experiment.get("protocols", ["fri3", "fri9", "stir9to3"])
    if not isinstance(protocols, list):
        raise SystemExit(f"Preset protocols must be a list in {preset_path}")
    queries = experiment.get("queries")
    if isinstance(queries, list):
        queries = ",".join(str(item) for item in queries)
    elif queries is None:
        queries = ""
    else:
        queries = str(queries)
    values = {
        "PRESET_INDEX": run_index,
        "PRESET_TOTAL": run_total,
        "PRESET_SOURCE_INDEX": preset_index,
        "PRESET_EXPERIMENT_NAME": experiment.get("name", f"experiment_{preset_index}"),
        "PRESET_P": match.group(1),
        "PRESET_K_EXP": match.group(2),
        "PRESET_R": match.group(3),
        "PRESET_N": experiment["n"],
        "PRESET_D": experiment["d"],
        "PRESET_FRI_SOUNDNESS_MODE": experiment.get("fri_soundness_mode", ""),
        "PRESET_FRI_REPETITIONS": experiment.get("fri_repetitions", ""),
        "PRESET_LAMBDA": experiment.get("lambda_target", 128),
        "PRESET_POW": experiment.get(
            "pow_bits", experiment.get("pow_bits_time", 0)
        ),
        "PRESET_SEC_MODE": experiment.get("sec_mode", ""),
        "PRESET_HASH_PROFILE": experiment.get("hash_profile", ""),
        "PRESET_STOP_DEGREE": experiment.get("stop_degree", ""),
        "PRESET_OOD_SAMPLES": experiment.get("ood_samples", ""),
        "PRESET_THREADS": experiment.get("threads", 1),
        "PRESET_WHIR_M": experiment.get("whir_m", ""),
        "PRESET_WHIR_BMAX": experiment.get("whir_bmax", ""),
        "PRESET_WHIR_R": experiment.get(
            "whir_r", experiment.get("whir_fixed_r", "")
        ),
        "PRESET_WHIR_RHO0": experiment.get("whir_rho0", ""),
        "PRESET_WHIR_POLYNOMIAL": experiment.get("whir_polynomial", ""),
        "PRESET_WHIR_REPETITIONS": experiment.get("whir_repetitions", ""),
        "PRESET_QUERIES": queries,
        "PRESET_PROTOCOLS": ",".join(str(item) for item in protocols),
    }
    return " ".join(f"{key}={shlex.quote(str(value))}" for key, value in values.items())

experiments = merged_experiments(data)
if not experiments:
    raise SystemExit(f"Preset has no experiments: {preset_path}")
selected = parse_experiment_filter(experiment_filter, experiments)
for run_index, (preset_index, experiment) in enumerate(selected, start=1):
    print(shell_assignments(run_index, len(selected), preset_index, experiment))
PY
)

if [[ "${#PRESET_EXPERIMENTS[@]}" -eq 0 ]]; then
  echo "Preset has no runnable experiments: ${PRESET_PATH}" >&2
  exit 1
fi

TMP_DIR=$(mktemp -d)
trap 'rm -rf "${TMP_DIR}"' EXIT
CSV_WRITTEN=0
JSON_OUTPUTS=()

for preset_env in "${PRESET_EXPERIMENTS[@]}"; do
  eval "$preset_env"

  EXP_P_VALUE=${P_VALUE:-$PRESET_P}
  EXP_K_EXP=${K_EXP:-$PRESET_K_EXP}
  EXP_R_VALUE=${R_VALUE:-$PRESET_R}
  EXP_N_VALUE=${N_VALUE:-$PRESET_N}
  EXP_D_VALUE=${D_VALUE:-$PRESET_D}
  EXP_LAMBDA_TARGET=${LAMBDA_TARGET:-$PRESET_LAMBDA}
  EXP_POW_BITS=${POW_BITS:-$PRESET_POW}
  EXP_SEC_MODE=${SEC_MODE:-${PRESET_SEC_MODE:-ConjectureCapacity}}
  EXP_HASH_PROFILE=${HASH_PROFILE:-${PRESET_HASH_PROFILE:-STIR_NATIVE}}
  EXP_STOP_DEGREE=${STOP_DEGREE:-${PRESET_STOP_DEGREE:-9}}
  EXP_OOD_SAMPLES=${OOD_SAMPLES:-${PRESET_OOD_SAMPLES:-2}}
  EXP_THREADS=${THREADS:-$PRESET_THREADS}
  EXP_QUERIES=${QUERIES:-$PRESET_QUERIES}
  EXP_PROTOCOLS=${PROTOCOL_OVERRIDE:-$PRESET_PROTOCOLS}
  EXP_WHIR_M=${WHIR_M:-$PRESET_WHIR_M}
  EXP_WHIR_BMAX=${WHIR_BMAX:-$PRESET_WHIR_BMAX}
  EXP_WHIR_R=${WHIR_R:-$PRESET_WHIR_R}
  EXP_WHIR_RHO0=${WHIR_RHO0:-$PRESET_WHIR_RHO0}
  EXP_WHIR_POLYNOMIAL=${WHIR_POLYNOMIAL:-$PRESET_WHIR_POLYNOMIAL}
  EXP_WHIR_REPETITIONS=${WHIR_REPETITIONS:-$PRESET_WHIR_REPETITIONS}

  EXP_FRI_SOUNDNESS_MODE=${FRI_SOUNDNESS_MODE}
  if [[ -z "${EXP_FRI_SOUNDNESS_MODE}" ]]; then
    if [[ -n "${PRESET_FRI_SOUNDNESS_MODE}" ]]; then
      EXP_FRI_SOUNDNESS_MODE="${PRESET_FRI_SOUNDNESS_MODE}"
    elif [[ -n "${PRESET_FRI_REPETITIONS}" ]]; then
      EXP_FRI_SOUNDNESS_MODE="manual_repetition"
    else
      EXP_FRI_SOUNDNESS_MODE="theorem_auto"
    fi
  fi

  if [[ "${EXP_FRI_SOUNDNESS_MODE}" != "theorem_auto" &&
        "${EXP_FRI_SOUNDNESS_MODE}" != "manual_repetition" ]]; then
    echo "FRI soundness mode must be theorem_auto or manual_repetition, got: ${EXP_FRI_SOUNDNESS_MODE}" >&2
    exit 1
  fi

  EXP_FRI_REPETITIONS=${FRI_REPETITIONS}
  if [[ "${EXP_FRI_SOUNDNESS_MODE}" == "manual_repetition" ]]; then
    EXP_FRI_REPETITIONS=${EXP_FRI_REPETITIONS:-$PRESET_FRI_REPETITIONS}
    if [[ -z "${EXP_FRI_REPETITIONS}" ]]; then
      echo "--fri-repetitions is required when FRI soundness mode is manual_repetition" >&2
      exit 1
    fi
  fi

  if [[ "${EXP_PROTOCOLS}" == "all" ]]; then
    EXP_PROTOCOLS="fri3,fri9,stir9to3,whir_gr_ud"
  fi

  ARGS_COMMON=(
    --p "${EXP_P_VALUE}"
    --k-exp "${EXP_K_EXP}"
    --r "${EXP_R_VALUE}"
    --n "${EXP_N_VALUE}"
    --d "${EXP_D_VALUE}"
    --fri-soundness-mode "${EXP_FRI_SOUNDNESS_MODE}"
    --lambda "${EXP_LAMBDA_TARGET}"
    --pow-bits "${EXP_POW_BITS}"
    --sec-mode "${EXP_SEC_MODE}"
    --hash-profile "${EXP_HASH_PROFILE}"
    --stop-degree "${EXP_STOP_DEGREE}"
    --ood-samples "${EXP_OOD_SAMPLES}"
    --threads "${EXP_THREADS}"
    --warmup "${WARMUP}"
    --reps "${REPS}"
    --format "${FORMAT}"
  )

  if [[ -n "${EXP_FRI_REPETITIONS}" ]]; then
    ARGS_COMMON+=(--fri-repetitions "${EXP_FRI_REPETITIONS}")
  fi

  if [[ -n "${EXP_QUERIES}" ]]; then
    ARGS_COMMON+=(--queries "${EXP_QUERIES}")
  fi

  if [[ -n "${EXP_WHIR_M}" ]]; then
    ARGS_COMMON+=(--whir-m "${EXP_WHIR_M}")
  fi

  if [[ -n "${EXP_WHIR_BMAX}" ]]; then
    ARGS_COMMON+=(--whir-bmax "${EXP_WHIR_BMAX}")
  fi

  if [[ -n "${EXP_WHIR_R}" ]]; then
    ARGS_COMMON+=(--whir-r "${EXP_WHIR_R}")
  fi

  if [[ -n "${EXP_WHIR_RHO0}" ]]; then
    ARGS_COMMON+=(--whir-rho0 "${EXP_WHIR_RHO0}")
  fi

  if [[ -n "${EXP_WHIR_POLYNOMIAL}" ]]; then
    ARGS_COMMON+=(--whir-polynomial "${EXP_WHIR_POLYNOMIAL}")
  fi

  if [[ -n "${EXP_WHIR_REPETITIONS}" ]]; then
    ARGS_COMMON+=(--whir-repetitions "${EXP_WHIR_REPETITIONS}")
  fi

  CMD=("${BENCH_BIN}" --protocol "${EXP_PROTOCOLS}" "${ARGS_COMMON[@]}")
  TMP_OUTPUT="${TMP_DIR}/experiment_${PRESET_INDEX}.${FORMAT}"
  echo "[bench ${PRESET_INDEX}/${PRESET_TOTAL}: ${PRESET_EXPERIMENT_NAME}] OMP_NUM_THREADS=${EXP_THREADS} OMP_DYNAMIC=false ${CMD[*]}" >&2
  OMP_NUM_THREADS="${EXP_THREADS}" OMP_DYNAMIC=false "${CMD[@]}" > "${TMP_OUTPUT}"

  if [[ "${FORMAT}" == "csv" ]]; then
    if [[ "${CSV_WRITTEN}" -eq 0 ]]; then
      cat "${TMP_OUTPUT}" > "${OUTPUT_PATH}"
      CSV_WRITTEN=1
    else
      tail -n +2 "${TMP_OUTPUT}" >> "${OUTPUT_PATH}"
    fi
  else
    JSON_OUTPUTS+=("${TMP_OUTPUT}")
  fi
done

if [[ "${FORMAT}" == "json" ]]; then
  python3 - "${OUTPUT_PATH}" "${JSON_OUTPUTS[@]}" <<'PY'
import json
import pathlib
import sys

out_path = pathlib.Path(sys.argv[1])
rows = []
for raw_path in sys.argv[2:]:
    rows.extend(json.loads(pathlib.Path(raw_path).read_text()))
out_path.write_text(json.dumps(rows, indent=2) + "\n")
PY
fi

echo "[bench] wrote ${OUTPUT_PATH}" >&2
printf '%s\n' "${OUTPUT_PATH}"
