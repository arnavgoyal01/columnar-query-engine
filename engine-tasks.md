# Columnar Query Engine — Phase 1 Staged Challenge

A LeetCode/Codecrafters-style breakdown of Phase 1 of the data-systems track: a small in-memory analytics engine that runs SQL-ish queries over **columnar** storage. Same rhythm as the file server — each stage is one testable capability; don't start a stage until the previous one passes.

**Dataset:** NYC Yellow Taxi, one month, as **CSV** (Parquet comes in Phase 3). ~3M rows. The automated tester uses its own tiny fixture, so you don't need the taxi file to run it — but build and eyeball against the real thing too.

---

## The CLI contract (how the tester drives your engine)

Your engine is your own C++ program, so — like the file-server harness assumed "takes a port, serves from cwd" — the tester needs a way to invoke it. Make your program accept these subcommands and print results in the format shown. Internally you still build the operator tree by hand in C++ (no SQL parser); this CLI is just a thin dispatch that exercises the operators.

```
<engine> schema  <csv>
<engine> select  <csv> <col1,col2,...> <limit>
<engine> filter  <csv> <col> <op> <value>          # op: == != < <= > >=
<engine> agg     <csv> <func> <col>                # func: sum count avg min max
<engine> groupby <csv> <groupcol> <func> <aggcol>
```

**Output format** (the harness is lenient about surrounding text — it extracts numbers — but strict about the values):

| command | prints |
|---|---|
| `schema` | a line `rows: <N>`, then one `<name>:<type>` per column (type ∈ `int64`,`double`,`string`) |
| `select` | `<limit>` lines, the chosen columns comma-separated, in the requested order |
| `filter` | a line containing `count: <N>` (number of matching rows) |
| `agg` | the single result value (a number) |
| `groupby` | one line per group: `<key> <value>` |

**Run the tester:**
```bash
python3 engine_test.py ./engine          # or however you invoke your binary
```

---

## Stages

### Stage 1 — Column storage + CSV load
**Goal:** get the data off disk into a column-oriented, typed, in-memory layout.
**Build:** a `Column` that holds a contiguous typed array (start with `std::variant<vector<int64_t>, vector<double>, vector<string>>`), a `Table` of named columns, a hand-rolled CSV parser, and simple type inference (try int → double → string per column).
**Passes when:** `schema` reports the right row count and the correct type for every column, and a file containing empty fields loads without crashing.
**Watch for:** columns must be *contiguous per-type arrays*, NOT a `vector<struct Row>` — the row-struct layout is the intuitive-but-wrong choice this whole project is built to avoid. Decide *something* for nulls (a parallel is-null bitmask is the standard answer; a sentinel is the quick hack) and note which.

### Stage 2 — Scan + projection
**Goal:** the iterator/volcano interface, and `SELECT col_a, col_b`.
**Build:** an operator interface (`open` / `next` / `close`) even though scan/projection are trivial — that interface is the contract everything else plugs into, so establish it now. Implement scan (produce rows/batches from a Table) and projection (keep only chosen columns).
**Passes when:** `select` returns exactly `<limit>` rows with the requested columns, correct and in order.

### Stage 3 — Filter (WHERE)
**Goal:** keep only rows matching a predicate.
**Build:** a filter operator wrapping a scan, supporting the six comparison ops on a numeric column.
**Passes when:** `filter` returns the correct match count for `==` and `>` (the harness checks both).

### Stage 4 — Scalar aggregation
**Goal:** `SUM` / `COUNT` / `AVG` / `MIN` / `MAX` over a column in one pass.
**Build:** an aggregation operator that consumes all input rows and emits one result.
**Passes when:** all five aggregates over `fare_amount` match the computed values (AVG within tolerance).

### Stage 5 — GROUP BY
**Goal:** the conceptual heart of analytics — per-group aggregates.
**Build:** a hash table keyed on the grouping column, accumulating the aggregate per group. `std::unordered_map` is fine to start; you'll replace it with your own later.
**Passes when:** `GROUP BY payment_type` with `SUM` and with `COUNT` both produce the right per-group values.

### Stage 6 — Validate against a reference (pandas)
**Goal:** the habit that carries through every later phase — prove correctness against an independent engine.
**Build:** nothing new in the engine; this stage runs your `groupby-sum` and `avg` over 10k generated rows and diffs against pandas.
**Passes when:** your results match pandas within tolerance. (Skips with a note if pandas isn't installed — `pip install pandas` to enable.)

---

## Suggested order & self-check

Do 1 → 6 in order, each green before the next, exactly like the file-server stages. By Stage 6 you have a small but real analytics engine and a validation harness you'll reuse in Phase 2 (where the same queries get 5–10× faster and you'll want to prove you didn't break correctness while optimizing).

**Not in scope for Phase 1** (deliberately): a SQL parser (build the operator tree by hand in C++), joins, and any performance work — those are Phase 2 and the capstone. Resist the urge; correctness first, speed second.
