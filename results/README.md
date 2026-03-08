# Archived Preset Evidence

Generated on `2026-03-07` for Batch 4 / Batch 5 tracking.

Artifacts:

- `results/smallest_workload_covering_all_protocols_gr216_r54_proof_size.csv`
- `results/smallest_workload_covering_all_protocols_gr216_r54_timing.csv`
- `results/main_benchmark_workload_gr216_r162_proof_size.csv`
- `results/main_benchmark_workload_gr216_r162_timing.csv`
- `results/workload_showing_two_stir_rounds_gr216_r486_proof_size.csv`
- `results/workload_showing_two_stir_rounds_gr216_r486_timing.csv`

Reproduction:

```bash
ctest --test-dir build --output-on-failure -R 'test_domain|test_fft3|test_folding|test_crypto|test_fri|test_stir'
./scripts/run_proof_size_estimator_from_preset.sh --preset bench/presets/smallest_workload_covering_all_protocols_gr216_r54.json --build-dir build --output results/smallest_workload_covering_all_protocols_gr216_r54_proof_size.csv
./scripts/run_timing_benchmark_from_preset.sh --preset bench/presets/smallest_workload_covering_all_protocols_gr216_r54.json --build-dir build --output results/smallest_workload_covering_all_protocols_gr216_r54_timing.csv --warmup 0 --reps 1
OMP_NUM_THREADS=1 ./scripts/run_proof_size_estimator_from_preset.sh --preset bench/presets/main_benchmark_workload_for_proof_size_gr216_r162.json --build-dir build-release --output results/main_benchmark_workload_gr216_r162_proof_size.csv
OMP_NUM_THREADS=1 ./scripts/run_timing_benchmark_from_preset.sh --preset bench/presets/main_benchmark_workload_for_timing_gr216_r162.json --build-dir build-release --output results/main_benchmark_workload_gr216_r162_timing.csv --warmup 1 --reps 3
OMP_NUM_THREADS=1 ./scripts/run_proof_size_estimator_from_preset.sh --preset bench/presets/workload_showing_two_stir_rounds_gr216_r486.json --build-dir build-release --output results/workload_showing_two_stir_rounds_gr216_r486_proof_size.csv
OMP_NUM_THREADS=1 ./scripts/run_timing_benchmark_from_preset.sh --preset bench/presets/workload_showing_two_stir_rounds_gr216_r486.json --build-dir build-release --output results/workload_showing_two_stir_rounds_gr216_r486_timing.csv --warmup 1 --reps 3
```

Notes:

- `smallest_workload_covering_all_protocols_gr216_r54_*` rows use `GR(2^16,54), n=81, d=27` and contain `fri3`, `fri9`, `stir9to3`.
- `main_benchmark_workload_gr216_r162_*` rows use `GR(2^16,162), n=243, d=81` and contain `fri3`, `fri9`, `stir9to3`.
- `workload_showing_two_stir_rounds_gr216_r486_proof_size.csv` uses `GR(2^16,486), n=729, d=243` and contains `fri3`, `fri9`, `stir9to3`; it exercises two STIR rounds (`729 -> 243 -> 81`) in the estimator path.
- `workload_showing_two_stir_rounds_gr216_r486_timing.csv` uses the same preset with `pow_bits = 0`, `warmup = 1`, `reps = 3`; in this measured path, `stir9to3` is still heavier than `fri9` on both `prover_total_ms` (`7370.485` vs `3930.196`) and `verify_ms` (`8240.489` vs `324.210`).
- Under the current estimator, `workload_showing_two_stir_rounds_gr216_r486_*` keeps `fri9` and `stir9to3` at the same `estimated_verifier_hashes = 14`, but `stir9to3` still has the larger `estimated_argument_bytes` (`36.570 KiB` vs `29.926 KiB`), so this preset is archived as a trend/result point rather than as first-wave time-bench headline evidence.
- `bench_time` reports measured `verifier_hashes_actual`; for `workload_showing_two_stir_rounds_gr216_r486_*`, the rows are `24 / 15 / 33` for `fri3 / fri9 / stir9to3`, so the time-side evidence is kept as a supplementary archive rather than a headline comparison.
- `scripts/run_proof_size_estimator_from_preset.sh` / `scripts/run_timing_benchmark_from_preset.sh` now run `csv/json` protocol lists in one process so Batch 4 presets reuse one warmed `GRContext`.
