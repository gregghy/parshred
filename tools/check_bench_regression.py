#!/usr/bin/env python3
"""Check a benchmark run against a baseline and fail on >threshold% regression.

Usage:
  check_bench_regression.py <baseline.json> <current.json> [--threshold PCT]

Exit codes:
  0 — no regression (or current improved); current.json is suitable as the
      new baseline.
  1 — at least one size regressed by more than --threshold percent.
  2 — usage error or unreadable input.

The comparison is on mbps_median per size_bytes. Sizes present in the
current run but missing from the baseline are reported as new (no failure).
Sizes in the baseline but missing from current are reported as missing
(failure, since a tracked size stopped being measured).
"""
import argparse
import json
import sys
from pathlib import Path


def load(path: str) -> dict:
    try:
        return json.loads(Path(path).read_text())
    except (OSError, json.JSONDecodeError) as e:
        print(f"error: cannot read {path}: {e}", file=sys.stderr)
        sys.exit(2)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("baseline")
    ap.add_argument("current")
    ap.add_argument("--threshold", type=float, default=10.0,
                    help="max allowed regression in percent (default: 10)")
    args = ap.parse_args()

    base = load(args.baseline)
    curr = load(args.current)

    base_by_size = {r["size_bytes"]: r for r in base.get("results", [])}
    curr_by_size = {r["size_bytes"]: r for r in curr.get("results", [])}

    regressions = []
    print(f"{'size':>12} {'baseline':>12} {'current':>12} {'delta':>10}  status")
    print("-" * 62)
    for size, br in sorted(base_by_size.items()):
        cr = curr_by_size.get(size)
        if cr is None:
            print(f"{size:>12} {br['mbps_median']:>10.0f}   {'—':>10} {'—':>10}  MISSING (failure)")
            regressions.append((size, br["mbps_median"], None))
            continue
        b = br["mbps_median"]
        c = cr["mbps_median"]
        delta_pct = (c - b) / b * 100.0
        status = "ok"
        if delta_pct < -args.threshold:
            status = f"REGRESSION (< -{args.threshold}%)"
            regressions.append((size, b, c))
        elif delta_pct > 0:
            status = "improved"
        print(f"{size:>12} {b:>10.0f}   {c:>10.0f}   {delta_pct:>7.1f}%  {status}")

    # Report sizes in current but not in baseline (new tracking points).
    for size in sorted(set(curr_by_size) - set(base_by_size)):
        cr = curr_by_size[size]
        print(f"{size:>12} {'—':>10}   {cr['mbps_median']:>10.0f}   {'—':>10}  new (no baseline)")

    if regressions:
        print(f"\nFAIL: {len(regressions)} size(s) regressed by more than "
              f"{args.threshold}% vs baseline.", file=sys.stderr)
        return 1
    print("\nOK: no regression vs baseline.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
