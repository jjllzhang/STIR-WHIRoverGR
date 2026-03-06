#!/usr/bin/env python3

import argparse
import csv
import pathlib
import sys
from typing import Iterable, List


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot Phase 6 bench CSV results grouped by protocol."
    )
    parser.add_argument("--input", required=True, help="Input CSV file")
    parser.add_argument(
        "--metric",
        required=True,
        help="Numeric CSV column to plot, e.g. estimated_argument_kib or prover_total_ms",
    )
    parser.add_argument(
        "--x-axis",
        default="n",
        help="Numeric CSV column for x-axis (default: n)",
    )
    parser.add_argument(
        "--output",
        default="",
        help="Output image path (default: alongside CSV as <stem>_<metric>.png)",
    )
    parser.add_argument(
        "--title",
        default="",
        help="Optional explicit plot title",
    )
    return parser.parse_args()


def require_matplotlib():
    try:
        import matplotlib.pyplot as plt  # type: ignore
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "matplotlib is required for plotting. Install it first, for example: "
            "python3 -m pip install matplotlib"
        ) from exc
    return plt


def load_rows(path: pathlib.Path) -> List[dict]:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        rows = list(reader)
    if not rows:
        raise SystemExit(f"No CSV rows found in {path}")
    return rows


def require_columns(rows: Iterable[dict], columns: List[str]) -> None:
    first = next(iter(rows))
    missing = [column for column in columns if column not in first]
    if missing:
        raise SystemExit(f"Missing required CSV columns: {', '.join(missing)}")


def to_float(row: dict, key: str) -> float:
    value = row.get(key, "")
    if value is None or value == "":
        raise SystemExit(f"Row is missing numeric value for column '{key}'")
    try:
        return float(value)
    except ValueError as exc:
        raise SystemExit(f"Column '{key}' contains non-numeric value: {value}") from exc


def main() -> int:
    args = parse_args()
    input_path = pathlib.Path(args.input)
    if not input_path.is_file():
        raise SystemExit(f"Input CSV not found: {input_path}")

    rows = load_rows(input_path)
    require_columns(rows, ["protocol", args.x_axis, args.metric])

    plt = require_matplotlib()

    grouped = {}
    for row in rows:
        protocol = row["protocol"]
        grouped.setdefault(protocol, []).append(row)

    figure, axis = plt.subplots(figsize=(8, 5))
    for protocol, protocol_rows in sorted(grouped.items()):
        sorted_rows = sorted(protocol_rows, key=lambda row: to_float(row, args.x_axis))
        xs = [to_float(row, args.x_axis) for row in sorted_rows]
        ys = [to_float(row, args.metric) for row in sorted_rows]
        axis.plot(xs, ys, marker="o", label=protocol)

    axis.set_xlabel(args.x_axis)
    axis.set_ylabel(args.metric)
    axis.set_title(args.title or f"{args.metric} vs {args.x_axis}")
    axis.grid(True, linestyle="--", alpha=0.35)
    axis.legend()
    figure.tight_layout()

    if args.output:
        output_path = pathlib.Path(args.output)
    else:
        output_path = input_path.with_name(f"{input_path.stem}_{args.metric}.png")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=160)
    print(output_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
