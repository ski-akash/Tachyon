cmake_minimum_required(VERSION 3.10)
project(QuillDB)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Core source files (everything EXCEPT main.cpp and benchmark.cpp)
set(CORE_SOURCES
    src/lexer/Lexer.cpp
    src/parser/Parser.cpp
    src/planner/Planner.cpp
    src/executor/Executor.cpp
    src/optimizer/Optimizer.cpp
    src/catalog/Catalog.cpp
)

include_directories(src)

# Executable 1: The standard CLI
add_executable(quilldb src/cli/main.cpp ${CORE_SOURCES})

# Executable 2: The Benchmark runner
add_executable(quilldb_benchmark src/cli/benchmark.cpp ${CORE_SOURCES})

# Executable 3: The Time-Series Tick Benchmark
add_executable(quilldb_ts_benchmark src/cli/tick_benchmark.cpp ${CORE_SOURCES})

# Executable 4: The Persistence and Compression Benchmark
add_executable(quilldb_flush_benchmark src/cli/flush_benchmark.cpp ${CORE_SOURCES})

add_executable(quilldb_query_benchmark src/cli/query_benchmark.cpp ${CORE_SOURCES})