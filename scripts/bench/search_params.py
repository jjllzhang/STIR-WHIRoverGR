#!/usr/bin/env python3

import argparse
import csv
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from datetime import datetime
from fractions import Fraction
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple

RING_RE = re.compile(r"GR\((\d+)\^(\d+),(\d+)\)")


@dataclass(frozen=True)
class RingConfig:
    p: int
    k_exp: int
    r: int


@dataclass(frozen=True)
class SoundnessConfig:
    lambda_target: int
    pow_bits: int
    sec_mode: str
    queries: str


@dataclass(frozen=True)
class SweepPoint:
    n: int
    d: int
    rho: str


@dataclass(frozen=True)
class WhirOptions:
    m: int
    bmax: int
    rho0: str
    polynomial: str
    fixed_r: Optional[int]
    repetitions: Optional[int]


class SearchError(RuntimeError):
    pass


def normalize_fri_soundness_mode(raw: str) -> str:
    value = raw.strip()
    if value not in {"theorem_auto", "manual_repetition"}:
        raise SearchError(
            "fri soundness mode must be theorem_auto or manual_repetition"
        )
    return value


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Enumerate parameter candidates over bench_time, "
            "and emit combined CSV + markdown summary ranked by actual proof size."
        )
    )

    parser.add_argument("--preset", default="", help="Preset JSON path (optional)")
    parser.add_argument(
        "--experiments",
        "--experiment",
        default="",
        help=(
            "Run only selected preset experiment names or 1-based indices/ranges "
            "(comma list, e.g. 2,4-6 or gr216_r162_main)"
        ),
    )
    parser.add_argument(
        "--build-dir", default="build", help="Build directory containing bench binaries"
    )
    parser.add_argument(
        "--time-bin",
        default="",
        help="Path to bench_time (default: <build-dir>/bench_time)",
    )

    parser.add_argument(
        "--protocols",
        default="",
        help=(
            "Comma-separated protocols: fri3,fri9,stir9to3,whir_gr_ud "
            "(or all; default from preset or fri3,fri9,stir9to3)"
        ),
    )

    parser.add_argument("--n-values", default="", help="Comma-separated n sweep")
    parser.add_argument("--d-values", default="", help="Comma-separated d sweep")
    parser.add_argument(
        "--rho-values",
        default="",
        help="Comma-separated rho sweep, e.g. 1/3,1/9 (mutually exclusive with --d-values)",
    )

    parser.add_argument(
        "--soundness",
        action="append",
        default=[],
        help=(
            "Repeatable soundness tuple. Format: lambda:pow:sec-mode:queries "
            "or lambda,pow,sec-mode,queries. queries can be auto, theorem_auto, or q0[,q1,...]"
        ),
    )
    parser.add_argument(
        "--default-sec-mode",
        default="ConjectureCapacity",
        help="Fallback sec-mode when no --soundness is provided",
    )
    parser.add_argument(
        "--default-queries",
        default="auto",
        help="Fallback queries when no --soundness is provided (auto, theorem_auto, or q0[,q1,...])",
    )

    parser.add_argument(
        "--hash-profile",
        default="",
        help="Hash profile (default: preset value, else STIR_NATIVE)",
    )
    parser.add_argument(
        "--fri-soundness-mode",
        default="",
        help=(
            "FRI soundness mode: theorem_auto or manual_repetition "
            "(default: preset value, else theorem_auto; preset fri_repetitions "
            "without an explicit mode fall back to manual_repetition)"
        ),
    )
    parser.add_argument(
        "--fri-repetitions",
        type=int,
        default=None,
        help=(
            "Explicit FRI repetition count m. In theorem_auto this is an optional "
            "override that must be >= the required minimum; in "
            "manual_repetition it is the actual fixed m."
        ),
    )
    parser.add_argument("--stop-degree", type=int, default=None)
    parser.add_argument("--ood-samples", type=int, default=None)
    parser.add_argument("--threads", type=int, default=None)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--reps", type=int, default=3)
    parser.add_argument("--whir-m", type=int, default=None)
    parser.add_argument("--whir-bmax", type=int, default=None)
    parser.add_argument(
        "--whir-r",
        "--whir-fixed-r",
        dest="whir_r",
        type=int,
        default=None,
        help="Fixed WHIR extension degree. If omitted, bench_time still sees preset ring r via --r.",
    )
    parser.add_argument("--whir-rho0", default="")
    parser.add_argument(
        "--whir-polynomial",
        default="",
        help="WHIR polynomial family: multiquadratic or multilinear",
    )
    parser.add_argument("--whir-repetitions", type=int, default=None)

    parser.add_argument("--include-time", action="store_true")
    parser.add_argument(
        "--time-metric",
        default="prover_total_ms",
        choices=["prover_total_ms", "verify_ms"],
        help="Metric used by time Top-K and Pareto",
    )

    parser.add_argument("--top-k", type=int, default=10, help="Top-K for proof-size ranking")
    parser.add_argument(
        "--time-top-k", type=int, default=10, help="Top-K for time ranking if include-time"
    )

    parser.add_argument(
        "--combined-csv",
        default="",
        help="Output combined CSV path (default: results/runs/<timestamp>/search_combined.csv)",
    )
    parser.add_argument(
        "--summary-md",
        default="",
        help="Output summary markdown path (default: results/runs/<timestamp>/search_summary.md)",
    )

    return parser.parse_args()


def parse_csv_list(raw: str) -> List[str]:
    if not raw.strip():
        return []
    return [item.strip() for item in raw.split(",") if item.strip()]


def parse_int_list(raw: str, flag: str) -> List[int]:
    values: List[int] = []
    for token in parse_csv_list(raw):
        try:
            value = int(token)
        except ValueError as exc:
            raise SearchError(f"invalid integer in {flag}: {token}") from exc
        if value <= 0:
            raise SearchError(f"{flag} values must be > 0, got {value}")
        values.append(value)
    return values


def parse_ratio_list(raw: str) -> List[Fraction]:
    values: List[Fraction] = []
    for token in parse_csv_list(raw):
        try:
            ratio = Fraction(token)
        except Exception as exc:
            raise SearchError(f"invalid rho ratio: {token}") from exc
        if ratio <= 0 or ratio >= 1:
            raise SearchError(f"rho must be in (0,1), got {token}")
        values.append(ratio)
    return values


def reduce_ratio(numer: int, denom: int) -> str:
    frac = Fraction(numer, denom)
    return f"{frac.numerator}/{frac.denominator}"


def parse_ring(raw_ring: str) -> RingConfig:
    match = RING_RE.fullmatch(raw_ring.strip())
    if not match:
        raise SearchError(f"unsupported ring format: {raw_ring}")
    p, k_exp, r = (int(match.group(1)), int(match.group(2)), int(match.group(3)))
    return RingConfig(p=p, k_exp=k_exp, r=r)


def load_preset(path: str) -> Dict[str, object]:
    if not path:
        return {}
    preset_path = Path(path)
    if not preset_path.is_file():
        raise SearchError(f"preset not found: {preset_path}")
    try:
        return json.loads(preset_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise SearchError(f"failed to parse preset json: {preset_path}") from exc


def expand_preset_experiments(preset: Dict[str, object]) -> List[Dict[str, object]]:
    if "experiments" not in preset:
        out = dict(preset)
        out.setdefault("name", "default")
        return [out]

    defaults = preset.get("defaults", {})
    if not isinstance(defaults, dict):
        raise SearchError("preset field 'defaults' must be an object")

    base = {
        key: value
        for key, value in preset.items()
        if key not in {"defaults", "experiments", "description", "notes"}
    }
    shared = {**base, **defaults}
    experiments = preset["experiments"]
    if not isinstance(experiments, list):
        raise SearchError("preset field 'experiments' must be a list")

    out: List[Dict[str, object]] = []
    for index, experiment in enumerate(experiments, start=1):
        if not isinstance(experiment, dict):
            raise SearchError(f"preset experiment #{index} must be an object")
        merged = {**shared, **experiment}
        merged.setdefault("name", f"experiment_{index}")
        out.append(merged)
    if not out:
        raise SearchError("preset has no experiments")
    return out


def filter_preset_experiments(
    experiments: Sequence[Dict[str, object]], raw_filter: str
) -> List[Tuple[int, Dict[str, object]]]:
    if not raw_filter.strip():
        return list(enumerate(experiments, start=1))

    names = {
        str(experiment.get("name", f"experiment_{index}")): index
        for index, experiment in enumerate(experiments, start=1)
    }
    selected: List[Tuple[int, Dict[str, object]]] = []
    seen = set()
    for token in parse_csv_list(raw_filter):
        indices: List[int]
        if token in names:
            indices = [names[token]]
        elif re.fullmatch(r"[0-9]+", token):
            indices = [int(token)]
        elif re.fullmatch(r"[0-9]+-[0-9]+", token):
            begin, end = (int(part) for part in token.split("-", 1))
            if begin > end:
                raise SearchError(f"invalid descending experiment range: {token}")
            indices = list(range(begin, end + 1))
        else:
            available = ", ".join(
                f"{index}:{experiment.get('name', f'experiment_{index}')}"
                for index, experiment in enumerate(experiments, start=1)
            )
            raise SearchError(
                f"unknown experiment selector '{token}'. Available: {available}"
            )
        for index in indices:
            if index < 1 or index > len(experiments):
                raise SearchError(
                    f"experiment index {index} out of range 1..{len(experiments)}"
                )
            if index in seen:
                continue
            seen.add(index)
            selected.append((index, experiments[index - 1]))
    if not selected:
        raise SearchError(f"experiment filter selected no experiments: {raw_filter}")
    return selected


def resolve_protocols(args: argparse.Namespace, preset: Dict[str, object]) -> List[str]:
    allowed_order = ["fri3", "fri9", "stir9to3", "whir_gr_ud"]
    allowed = set(allowed_order)
    if args.protocols:
        raw_protocols = parse_csv_list(args.protocols)
    else:
        preset_protocols = preset.get("protocols", ["fri3", "fri9", "stir9to3"])
        if not isinstance(preset_protocols, list):
            raise SearchError("preset field 'protocols' must be a list")
        raw_protocols = [
            str(item).strip() for item in preset_protocols if str(item).strip()
        ]
    protocols: List[str] = []
    for protocol in raw_protocols:
        if protocol == "all":
            for expanded in allowed_order:
                if expanded not in protocols:
                    protocols.append(expanded)
            continue
        if protocol not in allowed:
            raise SearchError(f"unsupported protocol: {protocol}")
        if protocol not in protocols:
            protocols.append(protocol)
    if not protocols:
        raise SearchError("protocol list is empty")
    return protocols


def preset_optional_int(preset: Dict[str, object], *keys: str) -> Optional[int]:
    for key in keys:
        if key not in preset:
            continue
        raw_value = preset[key]
        if raw_value is None or str(raw_value).strip() == "":
            return None
        value = int(raw_value)
        if value <= 0:
            raise SearchError(f"preset {key} must be > 0")
        return value
    return None


def resolve_whir_options(
    args: argparse.Namespace, preset: Dict[str, object]
) -> WhirOptions:
    m = args.whir_m if args.whir_m is not None else int(preset.get("whir_m", 3))
    bmax = (
        args.whir_bmax
        if args.whir_bmax is not None
        else int(preset.get("whir_bmax", 1))
    )
    if m <= 0 or bmax <= 0:
        raise SearchError("WHIR m and bmax must be > 0")

    rho0 = args.whir_rho0 or str(preset.get("whir_rho0", "1/3"))
    try:
        rho0_ratio = Fraction(rho0)
    except Exception as exc:
        raise SearchError(f"invalid WHIR rho0: {rho0}") from exc
    if rho0_ratio <= 0 or rho0_ratio >= 1:
        raise SearchError(f"WHIR rho0 must be in (0,1), got {rho0}")
    rho0 = f"{rho0_ratio.numerator}/{rho0_ratio.denominator}"

    polynomial = args.whir_polynomial or str(
        preset.get("whir_polynomial", "multiquadratic")
    )
    polynomial = polynomial.strip().lower()
    if polynomial in {"multi_quadratic", "multi-quadratic"}:
        polynomial = "multiquadratic"
    if polynomial in {"multi_linear", "multi-linear"}:
        polynomial = "multilinear"
    if polynomial not in {"multiquadratic", "multilinear"}:
        raise SearchError(
            "WHIR polynomial must be multiquadratic or multilinear"
        )

    fixed_r = (
        args.whir_r
        if args.whir_r is not None
        else preset_optional_int(preset, "whir_r", "whir_fixed_r")
    )
    if fixed_r is not None and fixed_r <= 0:
        raise SearchError("WHIR fixed r must be > 0")

    repetitions = (
        args.whir_repetitions
        if args.whir_repetitions is not None
        else preset_optional_int(preset, "whir_repetitions")
    )
    if repetitions is not None and repetitions <= 0:
        raise SearchError("WHIR repetitions must be > 0")

    return WhirOptions(
        m=m,
        bmax=bmax,
        rho0=rho0,
        polynomial=polynomial,
        fixed_r=fixed_r,
        repetitions=repetitions,
    )


def parse_soundness_item(raw: str) -> SoundnessConfig:
    if ":" in raw:
        parts = [part.strip() for part in raw.split(":")]
    else:
        parts = [part.strip() for part in raw.split(",")]

    if len(parts) not in (3, 4):
        raise SearchError(
            f"invalid --soundness '{raw}', expected lambda:pow:sec-mode[:queries]"
        )

    try:
        lambda_target = int(parts[0])
        pow_bits = int(parts[1])
    except ValueError as exc:
        raise SearchError(f"invalid --soundness numeric fields: {raw}") from exc

    sec_mode = parts[2]
    queries = parts[3] if len(parts) == 4 and parts[3] else "auto"

    if lambda_target <= 0 or pow_bits < 0:
        raise SearchError(f"invalid --soundness bounds: {raw}")

    return SoundnessConfig(
        lambda_target=lambda_target,
        pow_bits=pow_bits,
        sec_mode=sec_mode,
        queries=queries,
    )


def preset_default_pow(preset: Dict[str, object]) -> int:
    for key in ("pow_bits", "pow_bits_size", "pow_bits_time"):
        if key in preset:
            value = int(preset[key])
            if value < 0:
                raise SearchError(f"preset {key} must be >= 0")
            return value
    return 22


def preset_default_lambda(preset: Dict[str, object]) -> int:
    if "lambda_target" in preset:
        value = int(preset["lambda_target"])
        if value <= 0:
            raise SearchError("preset lambda_target must be > 0")
        return value
    return 128


def preset_default_queries(preset: Dict[str, object], fallback: str) -> str:
    queries = preset.get("queries")
    if queries is None:
        return fallback
    if isinstance(queries, list):
        return ",".join(str(item) for item in queries)
    return str(queries)


def resolve_soundness_configs(
    args: argparse.Namespace, preset: Dict[str, object]
) -> List[SoundnessConfig]:
    if args.soundness:
        return [parse_soundness_item(item) for item in args.soundness]

    return [
        SoundnessConfig(
            lambda_target=preset_default_lambda(preset),
            pow_bits=preset_default_pow(preset),
            sec_mode=args.default_sec_mode,
            queries=preset_default_queries(preset, args.default_queries),
        )
    ]


def resolve_fri_soundness_mode(
    args: argparse.Namespace, preset: Dict[str, object]
) -> str:
    if args.fri_soundness_mode:
        return normalize_fri_soundness_mode(args.fri_soundness_mode)
    preset_mode = preset.get("fri_soundness_mode")
    if preset_mode is not None:
        return normalize_fri_soundness_mode(str(preset_mode))
    if "fri_repetitions" in preset:
        return "manual_repetition"
    return "theorem_auto"


def resolve_fri_repetitions(
    args: argparse.Namespace, preset: Dict[str, object], fri_soundness_mode: str
) -> Optional[int]:
    if args.fri_repetitions is not None:
        value = int(args.fri_repetitions)
    elif fri_soundness_mode == "manual_repetition" and "fri_repetitions" in preset:
        value = int(preset["fri_repetitions"])
    elif fri_soundness_mode == "manual_repetition":
        raise SearchError(
            "manual_repetition requires --fri-repetitions or preset fri_repetitions"
        )
    else:
        return None
    if value <= 0:
        raise SearchError("fri_repetitions must be > 0")
    return value


def resolve_sweep_points(args: argparse.Namespace, preset: Dict[str, object]) -> List[SweepPoint]:
    n_values = parse_int_list(args.n_values, "--n-values")
    d_values = parse_int_list(args.d_values, "--d-values")
    rho_values = parse_ratio_list(args.rho_values)

    if d_values and rho_values:
        raise SearchError("--d-values and --rho-values are mutually exclusive")

    if not n_values:
        if "n" not in preset:
            raise SearchError("missing n sweep: provide --n-values or preset n")
        n_values = [int(preset["n"])]

    points: List[SweepPoint] = []

    if d_values:
        for n in n_values:
            for d in d_values:
                if d >= n:
                    continue
                points.append(SweepPoint(n=n, d=d, rho=reduce_ratio(d, n)))
    elif rho_values:
        for n in n_values:
            for rho in rho_values:
                product = rho * n
                if product.denominator != 1:
                    continue
                d = product.numerator
                if d <= 0 or d >= n:
                    continue
                points.append(SweepPoint(n=n, d=d, rho=f"{rho.numerator}/{rho.denominator}"))
    else:
        if "d" not in preset:
            raise SearchError("missing degree sweep: provide --d-values/--rho-values or preset d")
        default_d = int(preset["d"])
        for n in n_values:
            if default_d < n:
                points.append(SweepPoint(n=n, d=default_d, rho=reduce_ratio(default_d, n)))

    dedup: Dict[Tuple[int, int], SweepPoint] = {}
    for point in points:
        dedup[(point.n, point.d)] = point

    out = list(dedup.values())
    out.sort(key=lambda item: (item.n, item.d))
    if not out:
        raise SearchError("no valid (n,d) sweep points generated")
    return out


def effective_runtime_args(
    args: argparse.Namespace, preset: Dict[str, object]
) -> argparse.Namespace:
    resolved = argparse.Namespace(**vars(args))
    resolved.hash_profile = args.hash_profile or str(
        preset.get("hash_profile", "STIR_NATIVE")
    )
    resolved.stop_degree = (
        args.stop_degree
        if args.stop_degree is not None
        else int(preset.get("stop_degree", 9))
    )
    resolved.ood_samples = (
        args.ood_samples
        if args.ood_samples is not None
        else int(preset.get("ood_samples", 2))
    )
    resolved.threads = (
        args.threads if args.threads is not None else int(preset.get("threads", 1))
    )
    return resolved


def run_csv_command(cmd: Sequence[str]) -> List[Dict[str, str]]:
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        detail = proc.stderr.strip() or proc.stdout.strip()
        raise SearchError(f"command failed ({proc.returncode}): {' '.join(cmd)}\n{detail}")

    content = proc.stdout.strip()
    if not content:
        raise SearchError(f"command produced empty output: {' '.join(cmd)}")

    reader = csv.DictReader(content.splitlines())
    rows = list(reader)
    if not rows:
        raise SearchError(f"command produced no csv rows: {' '.join(cmd)}")
    return rows


def to_float(value: str) -> Optional[float]:
    if value is None:
        return None
    text = str(value).strip()
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def build_time_command(
    time_bin: Path,
    ring: RingConfig,
    protocols: Sequence[str],
    point: SweepPoint,
    soundness: SoundnessConfig,
    fri_soundness_mode: str,
    fri_repetitions: Optional[int],
    whir_options: WhirOptions,
    args: argparse.Namespace,
) -> List[str]:
    cmd = [
        str(time_bin),
        "--protocol",
        ",".join(protocols),
        "--p",
        str(ring.p),
        "--k-exp",
        str(ring.k_exp),
        "--r",
        str(ring.r),
        "--n",
        str(point.n),
        "--d",
        str(point.d),
        "--fri-soundness-mode",
        fri_soundness_mode,
        "--lambda",
        str(soundness.lambda_target),
        "--pow-bits",
        str(soundness.pow_bits),
        "--sec-mode",
        soundness.sec_mode,
        "--hash-profile",
        args.hash_profile,
        "--stop-degree",
        str(args.stop_degree),
        "--ood-samples",
        str(args.ood_samples),
        "--threads",
        str(args.threads),
        "--warmup",
        str(args.warmup),
        "--reps",
        str(args.reps),
        "--format",
        "csv",
    ]
    if fri_repetitions is not None:
        cmd += ["--fri-repetitions", str(fri_repetitions)]
    if soundness.queries:
        cmd += ["--queries", soundness.queries]
    cmd += [
        "--whir-m",
        str(whir_options.m),
        "--whir-bmax",
        str(whir_options.bmax),
        "--whir-rho0",
        whir_options.rho0,
        "--whir-polynomial",
        whir_options.polynomial,
    ]
    if whir_options.fixed_r is not None:
        cmd += ["--whir-r", str(whir_options.fixed_r)]
    if whir_options.repetitions is not None:
        cmd += ["--whir-repetitions", str(whir_options.repetitions)]
    return cmd


def write_csv(path: Path, rows: List[Dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        raise SearchError("no rows to write")

    preferred = [
        "preset_experiment",
        "search_candidate_id",
        "search_soundness_id",
        "search_queries_spec",
        "search_time_metric",
        "protocol",
        "ring",
        "n",
        "d",
        "rho",
        "soundness_mode",
        "fri_repetitions",
        "lambda_target",
        "pow_bits",
        "sec_mode",
        "hash_profile",
        "soundness_model",
        "soundness_scope",
        "query_policy",
        "pow_policy",
        "effective_security_bits",
        "soundness_notes",
        "fold",
        "shift_power",
        "stop_degree",
        "ood_samples",
        "threads",
        "warmup",
        "reps",
        "commit_ms",
        "prove_query_phase_ms",
        "prover_total_ms",
        "verify_ms",
        "serialized_bytes_actual",
        "serialized_kib_actual",
        "verifier_hashes_actual",
    ]

    keys = set()
    for row in rows:
        keys.update(row.keys())

    columns = [col for col in preferred if col in keys]
    columns.extend(sorted(keys - set(columns)))

    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def sorted_top_k(rows: List[Dict[str, str]], key: str, top_k: int) -> List[Dict[str, str]]:
    with_metric = []
    for row in rows:
        metric = to_float(row.get(key, ""))
        if metric is None:
            continue
        with_metric.append((metric, row))
    with_metric.sort(key=lambda item: item[0])
    return [item[1] for item in with_metric[:top_k]]


def pareto_front(rows: List[Dict[str, str]], size_key: str, time_key: str) -> List[Dict[str, str]]:
    points: List[Tuple[float, float, Dict[str, str]]] = []
    for row in rows:
        size_val = to_float(row.get(size_key, ""))
        time_val = to_float(row.get(time_key, ""))
        if size_val is None or time_val is None:
            continue
        points.append((size_val, time_val, row))

    points.sort(key=lambda item: (item[0], item[1]))
    best_time = float("inf")
    front: List[Dict[str, str]] = []
    for _, time_val, row in points:
        if time_val < best_time:
            front.append(row)
            best_time = time_val
    return front


def render_table(rows: List[Dict[str, str]], columns: Sequence[str]) -> str:
    if not rows:
        return "(empty)\n"

    lines = []
    header = "| " + " | ".join(columns) + " |"
    sep = "| " + " | ".join(["---"] * len(columns)) + " |"
    lines.extend([header, sep])
    for row in rows:
        line = "| " + " | ".join(str(row.get(col, "")) for col in columns) + " |"
        lines.append(line)
    return "\n".join(lines) + "\n"


def write_summary(
    path: Path,
    rows: List[Dict[str, str]],
    protocols: Sequence[str],
    include_time: bool,
    time_metric: str,
    top_k: int,
    time_top_k: int,
    meta: Dict[str, str],
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)

    content: List[str] = []
    content.append("# Parameter Search Summary\n")
    content.append(f"- Generated: {datetime.now().isoformat(timespec='seconds')}\n")
    content.append(f"- Total rows: {len(rows)}\n")
    content.append(f"- Protocols: {', '.join(protocols)}\n")
    for key in ("ring", "build_dir", "size_bin", "time_bin", "include_time"):
        if key == "size_bin":
            continue
        if key in meta:
            content.append(f"- {key}: `{meta[key]}`\n")

    size_cols = [
        "protocol",
        "n",
        "d",
        "rho",
        "soundness_mode",
        "fri_repetitions",
        "lambda_target",
        "pow_bits",
        "sec_mode",
        "soundness_scope",
        "effective_security_bits",
        "query_policy",
        "search_queries_spec",
        "serialized_kib_actual",
        "serialized_bytes_actual",
    ]

    content.append("\n## Top-K by Actual Proof Size (global)\n")
    content.append(render_table(sorted_top_k(rows, "serialized_bytes_actual", top_k), size_cols))

    for protocol in protocols:
        subset = [row for row in rows if row.get("protocol") == protocol]
        content.append(f"\n## Top-K by Actual Proof Size ({protocol})\n")
        content.append(render_table(sorted_top_k(subset, "serialized_bytes_actual", top_k), size_cols))

    if include_time:
        time_cols = [
            "protocol",
            "n",
            "d",
            "rho",
            "soundness_mode",
            "fri_repetitions",
            "lambda_target",
            "pow_bits",
            "sec_mode",
            "soundness_scope",
            "effective_security_bits",
            "query_policy",
            "search_queries_spec",
            time_metric,
            "serialized_kib_actual",
        ]
        content.append(f"\n## Top-K by {time_metric} (global)\n")
        content.append(render_table(sorted_top_k(rows, time_metric, time_top_k), time_cols))

        for protocol in protocols:
            subset = [row for row in rows if row.get("protocol") == protocol]
            content.append(f"\n## Top-K by {time_metric} ({protocol})\n")
            content.append(
                render_table(sorted_top_k(subset, time_metric, time_top_k), time_cols)
            )

        content.append(
            f"\n## Pareto Front (Actual Proof Size vs {time_metric}, global)\n"
        )
        content.append(
            render_table(
                pareto_front(rows, "serialized_bytes_actual", time_metric),
                [
                    "protocol",
                    "n",
                    "d",
                    "rho",
                    "soundness_mode",
                    "fri_repetitions",
                    "lambda_target",
                    "pow_bits",
                    "sec_mode",
                    "soundness_scope",
                    "effective_security_bits",
                    "query_policy",
                    "search_queries_spec",
                    "serialized_kib_actual",
                    time_metric,
                ],
            )
        )

        for protocol in protocols:
            subset = [row for row in rows if row.get("protocol") == protocol]
            content.append(
                f"\n## Pareto Front (Actual Proof Size vs {time_metric}, {protocol})\n"
            )
            content.append(
                render_table(
                    pareto_front(subset, "serialized_bytes_actual", time_metric),
                    [
                        "protocol",
                        "n",
                        "d",
                        "rho",
                        "soundness_mode",
                        "fri_repetitions",
                        "lambda_target",
                        "pow_bits",
                        "sec_mode",
                        "soundness_scope",
                        "effective_security_bits",
                        "query_policy",
                        "search_queries_spec",
                        "serialized_kib_actual",
                        time_metric,
                    ],
                )
            )

    path.write_text("".join(content), encoding="utf-8")


def resolve_output_paths(args: argparse.Namespace) -> Tuple[Path, Path]:
    if args.combined_csv and args.summary_md:
        return Path(args.combined_csv), Path(args.summary_md)

    repo_root = Path(__file__).resolve().parents[2]
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    run_dir = repo_root / "results" / "runs" / timestamp
    combined_path = (
        Path(args.combined_csv)
        if args.combined_csv
        else run_dir / "search_combined.csv"
    )
    summary_path = (
        Path(args.summary_md)
        if args.summary_md
        else run_dir / "search_summary.md"
    )
    return combined_path, summary_path


def main() -> int:
    args = parse_args()
    preset_doc = load_preset(args.preset)
    experiments = filter_preset_experiments(
        expand_preset_experiments(preset_doc), args.experiments
    )

    build_dir = Path(args.build_dir)
    time_bin = Path(args.time_bin) if args.time_bin else (build_dir / "bench_time")

    if not time_bin.is_file():
        raise SearchError(f"time bench binary not found: {time_bin}")

    results: List[Dict[str, str]] = []
    all_protocols: List[str] = []
    rings: List[str] = []
    candidate_id = 0

    for experiment_index, (preset_source_index, preset) in enumerate(
        experiments, start=1
    ):
        experiment_name = str(preset.get("name", f"experiment_{experiment_index}"))
        if "ring" in preset:
            ring = parse_ring(str(preset["ring"]))
        else:
            raise SearchError(f"preset experiment {experiment_name} is missing ring")

        ring_label = f"GR({ring.p}^{ring.k_exp},{ring.r})"
        if ring_label not in rings:
            rings.append(ring_label)

        protocols = resolve_protocols(args, preset)
        for protocol in protocols:
            if protocol not in all_protocols:
                all_protocols.append(protocol)

        soundness_list = resolve_soundness_configs(args, preset)
        fri_soundness_mode = resolve_fri_soundness_mode(args, preset)
        fri_repetitions = resolve_fri_repetitions(args, preset, fri_soundness_mode)
        whir_options = resolve_whir_options(args, preset)
        sweep_points = resolve_sweep_points(args, preset)
        runtime_args = effective_runtime_args(args, preset)

        total = len(sweep_points) * len(soundness_list)
        local_candidate_id = 0
        for point in sweep_points:
            for soundness_idx, soundness in enumerate(soundness_list):
                candidate_id += 1
                local_candidate_id += 1
                print(
                    (
                        f"[search] experiment {experiment_index}/{len(experiments)} "
                        f"{experiment_name} (preset index {preset_source_index}), "
                        f"candidate {candidate_id} "
                        f"(local {local_candidate_id}/{total}): "
                        f"n={point.n}, d={point.d}, rho={point.rho}, "
                        f"fri_mode={fri_soundness_mode}, "
                        f"fri_m={'auto' if fri_repetitions is None else fri_repetitions}, "
                        f"lambda={soundness.lambda_target}, pow={soundness.pow_bits}, "
                        f"sec={soundness.sec_mode}, queries={soundness.queries}"
                    ),
                    file=sys.stderr,
                )

                time_cmd = build_time_command(
                    time_bin,
                    ring,
                    protocols,
                    point,
                    soundness,
                    fri_soundness_mode,
                    fri_repetitions,
                    whir_options,
                    runtime_args,
                )
                time_rows = run_csv_command(time_cmd)

                for row in time_rows:
                    merged: Dict[str, str] = {
                        "preset_experiment": experiment_name,
                        "search_candidate_id": str(candidate_id),
                        "search_soundness_id": str(soundness_idx),
                        "search_queries_spec": soundness.queries,
                        "search_time_metric": args.time_metric,
                    }
                    merged.update({key: str(value) for key, value in row.items()})
                    results.append(merged)

    combined_path, summary_path = resolve_output_paths(args)
    write_csv(combined_path, results)

    write_summary(
        path=summary_path,
        rows=results,
        protocols=all_protocols,
        include_time=args.include_time,
        time_metric=args.time_metric,
        top_k=args.top_k,
        time_top_k=args.time_top_k,
        meta={
            "ring": ", ".join(rings),
            "build_dir": str(build_dir),
            "time_bin": str(time_bin),
            "include_time": str(args.include_time).lower(),
        },
    )

    print(str(combined_path))
    print(str(summary_path))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except SearchError as exc:
        print(f"search_params failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
