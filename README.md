# Tachyon - Time Series Query Engine

An in-memory, high-performance C++ SQL query and time-series engine built from scratch — featuring a hand-written SQL compiler, rule- and cost-based query optimizer, vectorized columnar executor, and **B+ Tree / Hash indexing**.

QuillDB demonstrates zero-dependency database internals engineering: from raw SQL string tokenization to cache-friendly vectorized chunk execution and $O(\log N)$ temporal range scans over financial tick streams.

No external SQL/parsing libraries, no bundled storage engine — every stage of the pipeline is hand-written.

## Architecture

```mermaid
flowchart LR
    SQL["SQL text"] --> Lexer
    Lexer --> Parser
    Parser --> AST["AST\n(Statements / Expressions)"]
    AST --> Planner
    Planner --> LP["Logical plan\n(Scan, Filter, Join, Aggregate, Project)"]
    LP --> Optimizer["Optimizer\n(rule-based + cost-based)"]
    Optimizer --> PP["Physical plan"]
    PP --> Executor["Vectorized executor"]
    Executor --> Storage["Columnar storage\n(in-memory tables)"]
    Catalog["Catalog\n(row counts, distinct counts)"] -.-> Optimizer
```

Each stage is a distinct module under `src/`, mirroring how production query engines (Postgres, DuckDB) separate concerns:

| Stage | Directory | Responsibility |
|---|---|---|
| Lexer | `src/lexer` | Tokenizes raw SQL text into a token stream |
| Parser | `src/parser` | Recursive-descent parser → builds the AST |
| AST | `src/ast` | Statement/Expression node definitions (`SelectStatement`, `BinaryExpression`, `FunctionCall`, ...) |
| Planner | `src/planner` | Converts the AST into a tree of relational algebra operators (the logical plan) |
| Executor | `src/executor` | Walks the plan and executes it using the Volcano/iterator model over columnar `Chunk`s |
| Storage | `src/storage` | In-memory columnar `Table` / `Index` representation |
| Optimizer | `src/optimizer` | Rule-based + cost-based rewrites of the logical plan |
| Catalog | `src/catalog` | Row counts and index metadata used by the optimizer |
| CLI | `src/cli` | Entry point that wires the pipeline together, plus a standalone benchmark runner |

### Supported Quries

```sql
SELECT price FROM ticks WHERE time BETWEEN 1704067200500000000 AND 1704067200510000000;
```

```sql
SELECT col1, col2, SUM(col3)
FROM table_a
JOIN table_b ON table_a.id = table_b.a_id
WHERE col1 = 42
GROUP BY col1, col2;
```

- `SELECT` with column projection and aggregate functions (`SUM`, `COUNT`, ...)
- `FROM` with a single base table
- `JOIN ... ON <predicate>` (executed as a nested-loop join)
- `WHERE` with comparison operators (`=`, `>`, `<`, ...)
- `GROUP BY` with multiple grouping columns

- `EXPLAIN <query>` — prints the logical plan before and after optimization

### Execution model

Data is stored **columnar** (`Table::column_data_`, one vector per column) and executed **vectorized** — operators pull `Chunk`s (batches of rows, column-major) from their children rather than one row at a time, following the Volcano/iterator (`init()` / `next()`) execution model used by most real query engines.

## Build & run

Requires CMake 3.14+ and a C++17 compiler.

```powershell
# From the project root
mkdir build
cd build
cmake ..
cmake --build .

# Run
.\quilldb.exe          # Windows
./quilldb               # Linux / macOS
```

> The current `main.cpp` runs a fixed demo query (`EXPLAIN SELECT name FROM users WHERE id = 42;`) against a hardcoded in-memory table and prints the plan before and after optimization, so the index-selection rewrite is observable end-to-end without a REPL.

## Benchmarks

`src/cli/benchmark.cpp` builds a second executable, `quilldb_benchmark`, that isolates the payoff of the optimizer's automatic index selection: it runs `SELECT name FROM users WHERE id = <val>;` twice against the same table — once forcing a `Filter -> SeqScan` (what the naive Phase 1/2 planner would have produced) and once via the optimizer-selected `IndexScan` (a single `unordered_map` lookup in `Index`, see `src/storage/Index.h`) — and times both with `std::chrono::high_resolution_clock`.

```
cmake --build . --target quilldb_benchmark
./quilldb_benchmark        # Linux / macOS
.\quilldb_benchmark.exe    # Windows
```

The checked-in benchmark defaults to 1,000,000,000 rows, which needs more RAM than a typical dev machine has free just for two `std::string` columns held in memory. The results below were captured by building the same code with the row count turned down to 20,000,000 (`sed -i 's/1000000000/20000000/'`) on a single-core Linux sandbox (g++ -O2):

| Scan type | Plan | Time (20M rows) | 
|---|---|---|
| Sequential scan | `Filter -> SeqScan` | ~410–450 ms |
| Hash Index Lookup | `IndexScan` | ~2–3 µs |
| B+ Tree Range Scan | `TickScan` | ~10 ms |

## Roadmap

### Phase 1 — Front end ✅
- [x] Lexer: SQL text → tokens
- [x] Recursive-descent parser: tokens → AST
- [x] Logical planner: AST → relational algebra tree (`SeqScan`, `Filter`, `Project`)

### Phase 2 — Execution ✅
- [x] Columnar storage (`Table`, `Chunk`)
- [x] Vectorized (Volcano-model) executor
- [x] `JOIN ... ON` via nested-loop join
- [x] `GROUP BY` + aggregate functions (`AggregateNode`)

### Phase 3 — Optimizer ✅
- [x] **Rule-based optimization**: predicate pushdown, plus automatic index selection (rewrites `Filter + SeqScan` into `IndexScan` when the catalog reports a matching index)
- [x] **Catalog statistics**: track row counts and registered indexes per table (`Catalog`)
- [x] **Cost-based join selection**: `HashJoinNode` alongside the existing `NestedLoopJoinNode`; the optimizer picks between them at optimization time
- [x] **`EXPLAIN`**: `EXPLAIN <query>` plans + optimizes without executing, printing the plan tree via `PlanNode::toString()` before and after optimization

### Phase 4 Advanced Indexing & Time-Series ✅
- [x] **Advanced Indexing & Time-Series** — In-memory B+ Tree implementation for range scanning, temporal query planner (***TickScan***), and vectorized high-volume aggregation (***VWAP***).

## Project structure

```
quilldb/
├── CMakeLists.txt
├── src/
│   ├── lexer/       # Lexer.h / Lexer.cpp / TokenType.h
│   ├── parser/       # Parser.h / Parser.cpp
│   ├── ast/          # AST.h — Statement & Expression node definitions
│   ├── planner/       # Planner.h / Planner.cpp, LogicalPlan.h — operator tree
│   ├── optimizer/     # Optimizer.h / Optimizer.cpp — predicate pushdown, index selection, cost-based join choice
│   ├── catalog/       # Catalog.h / Catalog.cpp — row counts + index metadata
│   ├── executor/      # Executor.h / Executor.cpp — Volcano-model execution
│   ├── storage/       # Storage.h, Index.h — columnar Table / Chunk / hash Index
│   └── cli/          # main.cpp — entry point; benchmark.cpp — perf benchmark runner
└── build/            # CMake build output (generated)
```
