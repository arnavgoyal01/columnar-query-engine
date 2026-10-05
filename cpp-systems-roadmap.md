# C++ Systems Roadmap
### From Limit Order Book to Data-Intensive Systems

A complete path for an early-career C++ developer moving out of pure data science and into systems software — with a chosen specialization in **data-intensive systems** and a career destination of **data-platform / trading-firm infrastructure roles in Singapore and Hong Kong**.

---

## How to use this

This roadmap has one rule above all others: **depth over speed, and measure everything.** The month markers are elastic. If a phase takes twice as long because you went deep, that is the roadmap working, not failing. Never call something fast — prove it with a benchmark.

The structure is three stages:

1. **Foundations** — your own three projects plus two pieces of reusable infrastructure, covering networking, concurrency, and the OS.
2. **Specialization** — the data-intensive systems track, building on the foundations.
3. **Career bridge** — how it maps to real jobs in your target cities.

Your projects (the limit order book, the file server, the multi-user chat app) are not separate from this path. They *are* the foundations stage — each one teaches a core systems skill, and the two infrastructure pieces added below make your chat app and file server meaningfully better.

Rough total time for the core arc: about 9–12 months at part-time intensity. Elastic.

---

## The big picture at a glance

| Stage | Project | Networking | Concurrency | OS | Status |
|---|---|:---:|:---:|:---:|---|
| 0 | Limit order book | | * | | In progress |
| 1 | Thread pool + scheduler | | * | | New |
| 1 | File server (-> sync) | * | * | * | Your plan |
| 1 | Event loop / async runtime | * | * | * | New |
| 1 | Multi-user chat app | * | * | | Your plan |
| 2 | Columnar query engine | | * | | Track |
| 2 | Vectorize + parallelize | | * | | Track |
| 2 | Arrow / Parquet | | | * | Track |
| 2 | LSM-tree storage engine | | * | * | Track |
| 2 | Capstone (pick one) | (*) | * | (*) | Track |

---

## Stage 0 — Finish what you started

**Limit order book** *(in progress)*
*Teaches: concurrency, low-latency data structures, the performance mindset.*

Finish it before starting anything new. Beyond being a strong project in its own right, it's a near-perfect calling card for the trading firms that cluster in Singapore and Hong Kong — it signals you understand their domain before you've applied. Add a benchmark harness if it doesn't have one; that habit carries through the entire roadmap.

*Done when:* it handles a realistic order stream and you can quote its throughput and latency with numbers.

---

## Stage 1 — Foundations (concurrency / networking / OS)

This stage delivers everything from the original brief — networking, concurrency, and OS — and folds in your file server and chat app at the points where they fit best.

### 1.1 Thread pool + task scheduler  *(new)*
*Teaches: concurrency.*

A fixed pool of workers pulling from a queue, then futures so callers can `get()` a result, then work-stealing deques. This is the single most reusable thing you'll build — it goes straight into the chat app and, later, into parallel query execution in Stage 2. You'll learn mutexes, condition variables, false sharing, and why naive locking kills throughput.

*Done when:* it runs thousands of tasks across workers with measurable scaling, and exposes a clean futures API.

### 1.2 File server -> file sync tool  *(your plan, upgraded)*
*Teaches: networking, concurrency, OS file I/O.*

Start with the file server you planned (sockets + reading/writing files over the network). Then upgrade it into the better version of the idea you actually want: a mini Syncthing. Watch a directory with `inotify`, detect changes, and sync only the changed *blocks* using a rolling hash. This solves your real problem — moving files between devices without a thumb drive — far more elegantly, and `inotify` plus delta-transfer is an excellent OS-plus-algorithms combination.

*Done when:* editing a file on one device syncs only the changed bytes to another, automatically.

### 1.3 Event loop / async I/O runtime  *(new — do this before the chat app)*
*Teaches: concurrency, networking, OS.*

A mini libuv: wrap `epoll` (Linux) or `kqueue` (macOS) and expose a reactor where you register "call this callback when this socket is readable." This is the single most important OS networking primitive — it's what lets one server handle ten thousand connections on a couple of threads instead of one-thread-per-client.

*Done when:* a toy echo server built on it holds thousands of idle connections without spawning thousands of threads.

### 1.4 Multi-user chat app  *(your plan — the payoff)*
*Teaches: concurrency, networking at scale.*

Now build the chat app **on top of** the event loop and thread pool. This is where the foundations click together: the event loop handles the connections, the thread pool handles the work, and you get a server that scales properly instead of falling over at a few hundred users. Add rooms, presence, and message history.

*Done when:* it comfortably handles many simultaneous users with multiple rooms, built on your own event loop.

---

## Stage 2 — Specialization: data-intensive systems

The track you chose. Read-path first (queries returning answers is motivating), then make it fast, then real file formats, then the write-path, then one capstone.

### Phase 1 — Columnar query engine v1
*Dataset: NYC Yellow Taxi, one month (~3M rows). Swap in a domain you care about if you have one.*

A week-by-week breakdown:

- **Week 1 — Column storage + CSV loader.** Typed `Column` (int64/double/string) and a `Table`; a CSV parser to load data into columns. *Done when:* it loads the file and prints row count and schema.
- **Week 2 — Scan + projection.** Build the iterator/volcano interface (`open`/`next`/`close`) — the contract everything plugs into — and implement scan + `SELECT col_a, col_b`. *Done when:* you can select columns and print N rows.
- **Week 3 — Filter (`WHERE`).** Predicate evaluation with comparisons and `AND`/`OR`. *Done when:* filtered queries return the correct subset.
- **Week 4 — Scalar aggregation.** `SUM`/`COUNT`/`AVG`/`MIN`/`MAX` in a single pass. *Done when:* results match pandas on the same column.
- **Week 5 — `GROUP BY`.** Hash-based grouping accumulating aggregates per group. *Done when:* output matches a pandas `groupby`.
- **Week 6 — Compose + validate.** Chain operators (scan -> filter -> group -> aggregate); build the operator tree by hand in C++ (no SQL parser yet — execution before parsing). Add a harness that diffs your results against DuckDB. *Done when:* multi-operator queries match the reference engine.

*C++ you'll grow:* templates and `std::variant` for typed columns, RAII, move semantics, virtual interfaces for the volcano model.

### Phase 2 — Make it fast: vectorize + parallelize
Switch from row-at-a-time to batch execution (~1024 values per chunk, the DuckDB/ClickHouse model). Add an expression evaluator, try SIMD on a filter or aggregation, and parallelize scans across **your Stage 1 thread pool**. This is the phase that tells you whether you love this kind of work. *Done when:* a measured 5-10x speedup over v1, with the benchmark to prove it.

### Phase 3 — Real formats: Arrow + Parquet
Add a Parquet reader and adopt Apache Arrow as the in-memory layout. The project stops being a toy here — Arrow is the ecosystem's lingua franca, so you can validate against pandas or DuckDB on the same files. You'll learn compression and encoding (dictionary, run-length). *Done when:* the engine reads Parquet directly and matches DuckDB on the same query.

### Phase 4 — Storage engine: an LSM-tree
The write-path counterpart: in-memory memtable, write-ahead log, flush to sorted SSTables, compaction to merge them — the RocksDB pattern. This covers durability: `fsync`, crash recovery, write amplification, the LSM-vs-B-tree tradeoff. *Done when:* a key-value store survives a `kill -9` mid-write and recovers cleanly.

### Phase 5 — Capstone (pick by what you enjoyed)
- **Loved the query engine** -> add hash joins and a rule-based optimizer (predicate pushdown, join reordering).
- **Loved the *fast* part** -> a mini stream processor with windowed aggregations.
- **Loved the plumbing** -> distribute the query engine across processes with partitioned scans and a shuffle step.

*Done when:* one of these runs end-to-end, chosen by enjoyment rather than résumé value.

---

## Optional depth track (slot in by interest and time)

- **Memory allocator** *(OS)* — `malloc`/`free` over `mmap`, free lists, coalescing. Makes pointers and virtual memory stop being abstractions; sharpens the performance work in Stage 2.
- **Redis-style KV store** *(networking + concurrency + OS)* — a TCP server with a text protocol and a write-ahead log. A natural bridge between Stage 1 and Stage 2.
- **A Unix shell** *(OS)* — `fork`/`exec`/`wait`, pipes, signals, job control. The most efficient way to understand processes and file descriptors.
- **An `strace`-style tracer** *(OS, advanced)* — `ptrace` to log a process's syscalls. The deep end of OS internals; save it for last.

---

## Why this builds on your data science degree

You're not leaving data — you're descending into the engine room of the tools you already use.

- **You know the workloads.** When you build the `GROUP BY` operator, you already know what it's *for*. A pure-systems engineer has to learn that first; you don't.
- **You're building what made your tools fast.** Columnar storage and vectorized execution are *why* pandas and NumPy are fast. NumPy's speed is SIMD over contiguous arrays; pandas is columnar. Stage 2 builds that machinery.
- **You've already used the formats.** Parquet and Arrow are things you consumed through pandas or Spark. Building a reader closes a loop you didn't know was open.
- **Query optimization is applied statistics.** A cost-based optimizer estimates selectivity and join cardinality from data distributions; `COUNT DISTINCT` sketches like HyperLogLog are pure probability. Your stats training is a real edge in the part of this field systems engineers find hardest.

The motivating throughline: you're building the thing that fixes the slow queries and out-of-memory errors you've personally felt as a user.

---

## Where it leads

**Roles:** data infrastructure / data platform engineer, database or storage engineer, query-engine engineer, distributed-systems engineer.

**Employers split two ways:** data-tool companies (Databricks, Snowflake, ClickHouse, Confluent, Cockroach Labs, Starburst) and big-tech data-platform teams — plus, in your geography especially, the trading firms.

**The convergence worth noticing:** Singapore and Hong Kong are dense with trading firms (Optiver, IMC, Jump, Jane Street, Citadel Securities, HRT, Tower). The C++ data-systems skills this roadmap builds — performance, storage, vectorized execution — are exactly what those firms want for their platform and infrastructure roles. So this path doubles as an on-ramp back toward the low-latency world you started out curious about, in the exact cities you're aiming for. Your limit order book is the calling card.

**Compensation** *(rough, total comp, current to early-2026 knowledge, varies heavily by company tier — verify on levels.fyi and NodeFlair):*

- *Singapore (SGD):* junior ~90k-160k at top firms; senior ~150k-300k; staff/principal ~300k-500k+. Income tax tops ~24%, no capital gains tax.
- *Hong Kong (HKD):* junior ~450k-800k; senior ~900k-1.6M+; trading-firm dev roles can run well above. Salaries tax effectively capped ~15%, no capital gains — strong take-home at high incomes.

**The SG -> HK move** is well-trodden, especially within multi-office firms (most trading firms, big tech, and banks have both). A common route: a few years at a recognizable Singapore firm, then an internal transfer or direct HK application. Verify visa specifics with current official sources.

---

## Learning resources

- ***Designing Data-Intensive Applications*** (Kleppmann) — the conceptual bible; read it across the whole arc.
- **CMU 15-445** (Andy Pavlo) — free, project-based, exactly this material.
- ***Database Internals*** (Petrov) — storage-engine depth for Phase 4.
- **DuckDB source and blog** — a reference implementation to study from Phase 2 onward.

---

## The one rule, again

Depth over speed. Measure everything. Let what you enjoy building — not what sounds impressive — pick the corner of this field you settle into.
