#!/usr/bin/env python3
# ============================================================
#  bench.py  --  Phase 2 benchmark + correctness harness
# ============================================================
#  Phase 1 asked "is it correct?"  Phase 2 asks "is it FAST?" --
#  without breaking correctness. This harness does three things:
#    1. times your engine on a heavy query (median of repeated runs)
#    2. verifies the answer is still correct (independent computation)
#    3. compares against a saved baseline and reports the speedup
#
#  WORKFLOW
#  --------
#  1. Build your CURRENT (Phase 1) engine with -O3, then record the
#     baseline you'll improve against:
#         python3 bench.py ./engine --save-baseline
#  2. Apply an optimization, rebuild, then measure:
#         python3 bench.py ./engine
#     It prints the speedup vs the baseline and confirms correctness.
#  Repeat step 2 after every optimization. Goal: 5-10x, always green.
#
#  Uses the same CLI contract as Phase 1 (groupby / agg / filter).
#  Generates its own large synthetic dataset; no external deps.
#
#  TIMING CONTRACT (v2)
#  --------------------
#  Wall-clock time is dominated by CSV loading, which Phase 2 does not
#  touch, so speedups are computed on QUERY time reported by the engine.
#  Your engine must write exactly one line to STDERR of the form:
#
#         QUERY_MS: <milliseconds>
#
#  measured from "table loaded + types inferred" to "query finished,
#  before printing results". stdout is unchanged. If the line is missing
#  the run is treated as a failure (no silent fallback to wall-clock).
#  Wall-clock time is still shown alongside, for end-to-end context.
# ============================================================

import subprocess, sys, re, os, csv, random, time, json, statistics

# ---------- arg parsing (flags + the engine command) ----------
argv = sys.argv[1:]
ROWS = 3_000_000
REPEATS = 7
SAVE_BASELINE = False
DATA = None
BASELINE_FILE = "bench_baseline.json"
BASE_CMD = []
it = iter(range(len(argv)))
i = 0
while i < len(argv):
    a = argv[i]
    if a == "--rows": i += 1; ROWS = int(argv[i])
    elif a == "--repeats": i += 1; REPEATS = int(argv[i])
    elif a == "--save-baseline": SAVE_BASELINE = True
    elif a == "--data": i += 1; DATA = argv[i]
    elif a == "--baseline-file": i += 1; BASELINE_FILE = argv[i]
    else: BASE_CMD.append(a)
    i += 1

if not BASE_CMD:
    print("usage: python3 bench.py <how-to-run-your-engine> [--rows N] [--repeats N] [--save-baseline]")
    print("example: python3 bench.py ./engine --save-baseline")
    sys.exit(2)

# ---------- generate (and cache) a large synthetic dataset ----------
CATS = ["A", "B", "C", "D", "E", "F", "G", "H"]
if DATA is None:
    DATA = f"bench_data_{ROWS}.csv"
    if not os.path.exists(DATA):
        print(f"generating {ROWS:,}-row dataset -> {DATA} (one-time) ...", flush=True)
        random.seed(42)
        with open(DATA, "w", newline="") as f:
            w = csv.writer(f); w.writerow(["cat", "val", "grp"])
            for _ in range(ROWS):
                w.writerow([random.choice(CATS),
                            round(random.uniform(0, 100), 2),
                            random.randint(1, 100)])
    else:
        print(f"using cached dataset {DATA}")

# ---------- compute expected answers once (streaming, cached) ----------
EXP_FILE = DATA + ".expected.json"
if os.path.exists(EXP_FILE):
    expected = json.load(open(EXP_FILE))
else:
    print("computing reference answers (one-time) ...", flush=True)
    gsum = {}; total = 0.0; n = 0; filt = 0
    with open(DATA) as f:
        r = csv.reader(f); next(r)
        for row in r:
            c = row[0]; v = float(row[1])
            gsum[c] = gsum.get(c, 0.0) + v
            total += v; n += 1
            if v > 50: filt += 1
    expected = {"groupby_cat_sum_val": gsum, "sum_val": total,
                "avg_val": total / n, "filter_val_gt_50": filt, "rows": n}
    json.dump(expected, open(EXP_FILE, "w"))

# ---------- helpers ----------
NUM = re.compile(r"-?\d+\.?\d*")
def nums(s): return [float(x) for x in NUM.findall(s)]
def approx(a, b, tol=1e-2): return abs(a - b) <= tol + tol * abs(b)

def parse_groups(out):
    d = {}
    for ln in out.splitlines():
        if not ln.strip(): continue
        m = NUM.findall(ln)
        if not m: continue
        val = float(m[-1]); key = ln[:ln.rfind(m[-1])].strip().strip(":=,|\"' \t")
        d[key] = val
    return d

QMS = re.compile(r"QUERY_MS:\s*(-?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)")

def run_once(args):
    """returns (wall_seconds, query_ms or None, stdout)."""
    t0 = time.perf_counter()
    p = subprocess.run(BASE_CMD + args, capture_output=True, text=True, timeout=600)
    wall = time.perf_counter() - t0
    m = QMS.findall(p.stderr)
    qms = float(m[-1]) if m else None
    return wall, qms, p.stdout

def timed(args):
    """warm-up once (discarded), then median of REPEATS-1 runs.
    returns (median query_ms or None, median wall seconds, last stdout)."""
    run_once(args)                                   # warm cache, discard
    walls, qs = [], []
    last_out = ""
    for _ in range(max(1, REPEATS - 1)):
        wall, qms, out = run_once(args)
        walls.append(wall); last_out = out
        if qms is None:
            return None, statistics.median(walls), last_out
        qs.append(qms)
    return statistics.median(qs), statistics.median(walls), last_out

# ---------- the benchmark queries ----------
QUERIES = [
    ("groupby cat sum val", ["groupby", DATA, "cat", "sum", "val"], "groups"),
    ("agg sum val",         ["agg", DATA, "sum", "val"],            "scalar_sum"),
    ("filter val > 50",     ["filter", DATA, "val", ">", "50"],     "scalar_cnt"),
]

def check(kind, out):
    if kind == "groups":
        got = parse_groups(out); want = expected["groupby_cat_sum_val"]
        return len(got) == len(want) and all(k in got and approx(got[k], v) for k, v in want.items()), want, got
    if kind == "scalar_sum":
        g = nums(out); return (bool(g) and approx(g[-1], expected["sum_val"])), expected["sum_val"], (g[-1] if g else None)
    if kind == "scalar_cnt":
        g = nums(out); return (bool(g) and int(g[-1]) == expected["filter_val_gt_50"]), expected["filter_val_gt_50"], (int(g[-1]) if g else None)

# ---------- run ----------
print()
print(f"dataset: {DATA}  ({expected['rows']:,} rows)   repeats: {REPEATS} (median)")
print("=" * 64)

results = {}       # label -> query ms
walls = {}
all_correct = True
missing_timing = False
compared = False
print(f"{'':7s}{'query':22s}  {'query time':>12s}   {'throughput':>12s}   {'wall (incl. load)':>17s}")
for label, args, kind in QUERIES:
    qms, wall, out = timed(args)
    correct, want, got = check(kind, out)
    walls[label] = wall
    status = "OK  " if correct else "WRONG"
    if qms is None:
        missing_timing = True
        print(f"[{status}] {label:22s}  {'-- no QUERY_MS on stderr --':>30s}   {wall*1000:10.1f} ms")
    else:
        results[label] = qms
        thr = expected["rows"] / (qms / 1000) / 1e6 if qms > 0 else float("inf")
        print(f"[{status}] {label:22s}  {qms:9.2f} ms   {thr:8.1f} M r/s   {wall*1000:14.1f} ms")
    if not correct:
        all_correct = False
        print(f"         correctness FAILED: want {want}, got {got}")

# ---------- baseline save / compare ----------
print("=" * 64)
if missing_timing:
    print("TIMING MISSING: the engine must print 'QUERY_MS: <ms>' to stderr")
    print("(after load + type inference, before printing results).")
    print("No speedups computed and no baseline saved.")
elif SAVE_BASELINE:
    json.dump({"format": 2, "query_ms": results, "wall_s": walls},
              open(BASELINE_FILE, "w"), indent=2)
    print(f"baseline saved to {BASELINE_FILE}. Re-run without --save-baseline after optimizing.")
elif os.path.exists(BASELINE_FILE):
    base = json.load(open(BASELINE_FILE))
    if not isinstance(base, dict) or base.get("format") != 2:
        print(f"{BASELINE_FILE} was saved by the old harness (wall-clock incl. load),")
        print("so it can't be compared with query time. Re-baseline: add QUERY_MS")
        print("to your OLD whole-column build and run it with --save-baseline.")
    else:
        bq = base["query_ms"]
        print("query-time speedup vs baseline:")
        ratios = []
        for label, _, _ in QUERIES:
            if label in bq and results.get(label, 0) > 0:
                r = bq[label] / results[label]; ratios.append(r)
                flag = "  <-- 5x+ !" if r >= 5 else ("" if r >= 1 else "  (regressed)")
                print(f"    {label:22s}  {r:5.2f}x{flag}   ({bq[label]:.2f} -> {results[label]:.2f} ms)")
        compared = bool(ratios)
        if ratios:
            geo = statistics.geometric_mean(ratios)
            print(f"    {'overall (geomean)':22s}  {geo:5.2f}x")
else:
    print("no baseline found. Run once with --save-baseline on your baseline build first.")

print("=" * 64)
if not all_correct:
    print("RESULT: correctness BROKEN -- fix before trusting any speedup number.")
    sys.exit(1)
if missing_timing:
    print("RESULT: answers correct, but timing contract not met (see above).")
    sys.exit(1)
print("RESULT: all answers correct." + (" Speedups shown above." if compared else ""))
sys.exit(0)
