#!/usr/bin/env bash
# Compiles the Tachyon engine + bindings to WASM and drops tachyon.js /
# tachyon.wasm into web/, where the static frontend loads them.
set -euo pipefail

cd "$(dirname "$0")"

SOURCES="src/lexer/Lexer.cpp \
src/parser/Parser.cpp \
src/planner/Planner.cpp \
src/executor/Executor.cpp \
src/executor/PlanCompiler.cpp \
src/optimizer/Optimizer.cpp \
src/catalog/Catalog.cpp \
src/index/BTree.cpp \
src/database/Database.cpp \
src/wasm/bindings.cpp"

em++ -O3 -std=c++17 -Isrc \
    --bind \
    -s MODULARIZE=1 \
    -s EXPORT_NAME=createTachyonModule \
    -s ENVIRONMENT=web \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s NO_EXIT_RUNTIME=1 \
    $SOURCES \
    -o web/tachyon.js

echo "Built web/tachyon.js + web/tachyon.wasm"
