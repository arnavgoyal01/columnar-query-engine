#!/usr/bin/env python3
# ============================================================
#  engine_test.py  --  staged tester for the columnar query engine
# ============================================================
#  Usage:
#      python3 engine_test.py <how-to-run-your-engine>
#  Examples:
#      python3 engine_test.py ./engine
#      python3 engine_test.py ./build/qe
#
#  Your engine must implement this CLI contract (see the spec .md):
#     <engine> schema  <csv>
#     <engine> select  <csv> <col1,col2,...> <limit>
#     <engine> filter  <csv> <col> <op> <value>        (op: == != < <= > >=)
#     <engine> agg     <csv> <func> <col>              (func: sum count avg min max)
#     <engine> groupby <csv> <groupcol> <func> <aggcol>
#
#  Output contract:
#     schema  -> a line "rows: <N>", then one "<name>:<type>" per column
#                (type in {int64, double, string})
#     select  -> <limit> lines, the chosen columns comma-separated, in order
#     filter  -> a line containing "count: <N>"
#     agg     -> the single result value (a number)
#     groupby -> one line per group: "<key> <value>"
#
#  The harness is lenient about surrounding text (it extracts numbers),
#  but strict about the values. Small fixture, fast, no dependencies
#  except optional pandas for Stage 6.
# ============================================================

import subprocess, sys, re, tempfile, os, csv, random

if len(sys.argv) < 2:
    print("usage: python3 engine_test.py <how-to-run-your-engine>")
    print("example: python3 engine_test.py ./engine")
    sys.exit(2)

BASE_CMD = sys.argv[1:]

# ---------- fixture (answers computed from this, never hand-typed) ----------
HEADER = ["id", "passenger_count", "fare_amount", "payment_type"]
ROWS = [
    [1, 1, 10.0, "cash"],
    [2, 2, 20.0, "card"],
    [3, 1, 30.0, "card"],
    [4, 3, 40.0, "cash"],
    [5, 1, 15.0, "card"],
    [6, 2, 25.0, "cash"],
]
EXPECTED_TYPES = {
    "id": "int64", "passenger_count": "int64",
    "fare_amount": "double", "payment_type": "string",
}

_tmpdir = tempfile.mkdtemp(prefix="qe_test_")
MAIN_CSV = os.path.join(_tmpdir, "main.csv")
with open(MAIN_CSV, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(HEADER)
    for r in ROWS:
        w.writerow(r)

# a second fixture with nulls, only to confirm loading doesn't crash
NULL_CSV = os.path.join(_tmpdir, "nulls.csv")
with open(NULL_CSV, "w") as f:
    f.write("id,passenger_count,fare_amount,payment_type\n")
    f.write("1,1,10.0,cash\n")
    f.write("2,,20.0,\n")
    f.write("3,1,,card\n")

passed = 0
failed = 0

def ok(msg):
    global passed; passed += 1; print(f"    [PASS] {msg}")

def bad(msg):
    global failed; failed += 1; print(f"    [FAIL] {msg}")

def skip(msg):
    print(f"    [SKIP] {msg}")

def hdr(t):
    print(); print(f"=== {t} ===")

def run(args, timeout=30):
    """Run the engine with args; return (stdout, exit_ok)."""
    try:
        p = subprocess.run(BASE_CMD + args, capture_output=True,
                           text=True, timeout=timeout)
        return p.stdout, (p.returncode == 0)
    except subprocess.TimeoutExpired:
        return "", False
    except FileNotFoundError:
        print(f"    [FAIL] could not run: {' '.join(BASE_CMD)}  (is it built?)")
        return "", False

NUM = re.compile(r"-?\d+\.?\d*")

def nums(s):
    return [float(x) for x in NUM.findall(s)]

def approx(a, b, tol=1e-3):
    return abs(a - b) <= tol + tol * abs(b)

def col(idx):
    return [r[idx] for r in ROWS]

# ============================================================
#  STAGE 1 -- column storage + CSV load (schema)
# ============================================================
hdr("Stage 1: load CSV into typed columns, report schema")
out, good = run(["schema", MAIN_CSV])
if not good and not out:
    bad("engine did not run / produced no output for `schema`")
else:
    m = re.search(r"rows?\s*[:=]?\s*(\d+)", out, re.I)
    if m and int(m.group(1)) == len(ROWS):
        ok(f"row count = {len(ROWS)}")
    else:
        bad(f"row count wrong or missing (want {len(ROWS)}); got output:\n{out.strip()}")
    type_hits = 0
    for name, typ in EXPECTED_TYPES.items():
        # a line mentioning this column and its correct type
        pat = re.compile(rf"{re.escape(name)}\b.*\b{typ}\b", re.I)
        if pat.search(out):
            type_hits += 1
        else:
            bad(f"schema: expected {name}:{typ} not found")
    if type_hits == len(EXPECTED_TYPES):
        ok("all column types inferred correctly (int64/double/string)")

# loading a file with nulls should not crash
out2, good2 = run(["schema", NULL_CSV])
m2 = re.search(r"rows?\s*[:=]?\s*(\d+)", out2, re.I)
if m2 and int(m2.group(1)) == 3:
    ok("file containing nulls loads without crashing (3 rows)")
else:
    bad("did not load the null-containing file cleanly")

# ============================================================
#  STAGE 2 -- scan + projection (select)
# ============================================================
hdr("Stage 2: select specific columns (projection)")
out, good = run(["select", MAIN_CSV, "fare_amount,payment_type", "3"])
lines = [ln for ln in out.splitlines() if ln.strip()]
if len(lines) == 3:
    ok("returned exactly 3 rows")
    want_pay = ["cash", "card", "card"]      # first 3 rows' payment_type
    want_fare = [10.0, 20.0, 30.0]
    good_rows = 0
    for ln, wp, wf in zip(lines, want_pay, want_fare):
        parts = [p.strip() for p in ln.split(",")]
        ns = nums(ln)
        pay_ok = any(wp == p for p in parts)
        fare_ok = any(approx(n, wf) for n in ns)
        if pay_ok and fare_ok:
            good_rows += 1
    if good_rows == 3:
        ok("projected columns (fare_amount, payment_type) correct and in order")
    else:
        bad(f"projected values wrong; got:\n{out.strip()}")
else:
    bad(f"expected 3 rows, got {len(lines)}:\n{out.strip()}")

# ============================================================
#  STAGE 3 -- filter (WHERE)
# ============================================================
hdr("Stage 3: filter rows (WHERE)")
def filter_count(colname, op, val):
    out, _ = run(["filter", MAIN_CSV, colname, op, str(val)])
    m = re.search(r"count\s*[:=]?\s*(\d+)", out, re.I)
    if m: return int(m.group(1))
    ns = nums(out)
    return int(ns[-1]) if ns else None

c1 = filter_count("passenger_count", "==", 1)   # ids 1,3,5
exp1 = sum(1 for v in col(1) if v == 1)
if c1 == exp1: ok(f"passenger_count == 1  ->  {exp1} rows")
else: bad(f"passenger_count == 1: want {exp1}, got {c1}")

c2 = filter_count("fare_amount", ">", 20)        # 30,40,25
exp2 = sum(1 for v in col(2) if v > 20)
if c2 == exp2: ok(f"fare_amount > 20  ->  {exp2} rows")
else: bad(f"fare_amount > 20: want {exp2}, got {c2}")

# ============================================================
#  STAGE 4 -- scalar aggregation
# ============================================================
hdr("Stage 4: scalar aggregation (sum/count/avg/min/max)")
fares = col(2)
expected_aggs = {
    "sum":   sum(fares),
    "count": float(len(fares)),
    "avg":   sum(fares) / len(fares),
    "min":   min(fares),
    "max":   max(fares),
}
for func, want in expected_aggs.items():
    out, _ = run(["agg", MAIN_CSV, func, "fare_amount"])
    got = nums(out)
    if got and approx(got[-1], want):
        ok(f"{func}(fare_amount) = {want:g}")
    else:
        bad(f"{func}(fare_amount): want {want:g}, got {out.strip()!r}")

# ============================================================
#  STAGE 5 -- GROUP BY
# ============================================================
hdr("Stage 5: GROUP BY")
def parse_groups(out):
    d = {}
    for ln in out.splitlines():
        if not ln.strip(): continue
        ns = NUM.findall(ln)
        if not ns: continue
        val = float(ns[-1])
        key = ln
        # remove the numeric value we just read (last occurrence)
        key = key[:key.rfind(ns[-1])]
        key = key.strip().strip(":=,|\"' \t")
        d[key] = val
    return d

# groupby payment_type sum fare_amount
exp_sum = {}
for r in ROWS:
    exp_sum[r[3]] = exp_sum.get(r[3], 0.0) + r[2]
out, _ = run(["groupby", MAIN_CSV, "payment_type", "sum", "fare_amount"])
got = parse_groups(out)
if all(k in got and approx(got[k], v) for k, v in exp_sum.items()) and len(got) == len(exp_sum):
    ok("GROUP BY payment_type, SUM(fare_amount)  ->  " +
       ", ".join(f"{k}={v:g}" for k, v in exp_sum.items()))
else:
    bad(f"grouped sum wrong; want {exp_sum}, got {got}\nraw:\n{out.strip()}")

# groupby payment_type count
exp_cnt = {}
for r in ROWS:
    exp_cnt[r[3]] = exp_cnt.get(r[3], 0) + 1
out, _ = run(["groupby", MAIN_CSV, "payment_type", "count", "fare_amount"])
got = parse_groups(out)
if all(k in got and approx(got[k], v) for k, v in exp_cnt.items()) and len(got) == len(exp_cnt):
    ok("GROUP BY payment_type, COUNT  ->  " +
       ", ".join(f"{k}={int(v)}" for k, v in exp_cnt.items()))
else:
    bad(f"grouped count wrong; want {exp_cnt}, got {got}\nraw:\n{out.strip()}")

# ============================================================
#  STAGE 6 -- validate against a reference engine (pandas)
# ============================================================
hdr("Stage 6 [validation]: cross-check against pandas on larger data")
try:
    import pandas as pd
    random.seed(42)
    big = os.path.join(_tmpdir, "big.csv")
    cats = ["A", "B", "C", "D"]
    with open(big, "w", newline="") as f:
        w = csv.writer(f); w.writerow(["cat", "val", "grp"])
        for _ in range(10000):
            w.writerow([random.choice(cats), round(random.uniform(0, 100), 2),
                        random.randint(1, 5)])
    df = pd.read_csv(big)
    # engine groupby cat sum val  vs pandas
    out, _ = run(["groupby", big, "cat", "sum", "val"])
    got = parse_groups(out)
    want = df.groupby("cat")["val"].sum().to_dict()
    if got and all(k in got and approx(got[k], v, tol=1e-2) for k, v in want.items()):
        ok("groupby-sum matches pandas on 10k rows")
    else:
        bad(f"groupby-sum disagrees with pandas.\n  pandas: {want}\n  engine: {got}")
    # engine agg avg val vs pandas
    out, _ = run(["agg", big, "avg", "val"])
    ns = nums(out)
    if ns and approx(ns[-1], df["val"].mean(), tol=1e-2):
        ok("avg matches pandas on 10k rows")
    else:
        bad(f"avg disagrees with pandas: pandas={df['val'].mean():.4f}, engine={out.strip()!r}")
except ImportError:
    skip("pandas not installed; install it (pip install pandas) to cross-validate on real data")

# ============================================================
#  SUMMARY
# ============================================================
print()
print("=" * 60)
print(f"  RESULT:  {passed} passed,  {failed} failed")
print("=" * 60)
sys.exit(0 if failed == 0 else 1)
