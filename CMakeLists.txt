cmake_minimum_required(VERSION 3.10)
project(Tachyon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Core source files (everything EXCEPT main.cpp and benchmark.cpp)
set(CORE_SOURCES
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/planner/Planner.cpp
    src/executor/Executor.cpp
    src/executor/PlanCompiler.cpp
    src/optimizer/Optimizer.cpp
    src/catalog/Catalog.cpp
    src/index/BTree.cpp
    src/database/Database.cpp
)

include_directories(src)

# Executable 1: The standard CLI
add_executable(tachyon src/cli/main.cpp ${CORE_SOURCES})

# Executable 2: The Benchmark runner
add_executable(tachyon_benchmark src/cli/benchmark.cpp ${CORE_SOURCES})

# Executable 3: The Time-Series Tick Benchmark
add_executable(tachyon_ts_benchmark src/cli/tick_benchmark.cpp ${CORE_SOURCES})

# Executable 4: The Persistence and Compression Benchmark
add_executable(tachyon_flush_benchmark src/cli/flush_benchmark.cpp ${CORE_SOURCES})

add_executable(tachyon_query_benchmark src/cli/query_benchmark.cpp ${CORE_SOURCES})

add_executable(tachyon_vwap_benchmark src/cli/vwap_benchmark.cpp ${CORE_SOURCES})

add_executable(tachyon_btree_test src/cli/btree_test.cpp ${CORE_SOURCES})

# Interactive REPL: CREATE TABLE / INSERT / SELECT / EXPLAIN against two
# pre-seeded demo tables
add_executable(tachyon_repl src/cli/repl.cpp ${CORE_SOURCES})