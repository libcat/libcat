#!/bin/bash
__DIR__=$(cd "$(dirname "$0")" || exit 1; pwd); [ -z "${__DIR__}" ] && exit 1


if [ "$(uname | grep -i darwin)"x != ""x ]; then
  cpu_count="$(sysctl -n machdep.cpu.core_count)"
else
  cpu_count="$(/usr/bin/nproc)"
fi

cd "${__DIR__}" && mkdir -p build && cd build && \
cmake .. && \
make -j"${cpu_count}" && \
./cat_tests "$@"
