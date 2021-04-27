#!/bin/bash
__DIR__=$(cd "$(dirname "$0")" || exit 1; pwd); [ -z "${__DIR__}" ] && exit 1

cd "${__DIR__}" && mkdir -p build && cd build && \
cmake .. && \
cmake --build . --target cat_tests -- -j"$(${__DIR__}/tools/cpu_count.sh)" && \
./cat_tests $*
