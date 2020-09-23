#!/bin/bash -e
__DIR__=$(cd "$(dirname "$0")" || exit 1; pwd); [ -z "${__DIR__}" ] && exit 1
__PROJECT__="${__DIR__}/../"

if [ $# -ne 1 ] ; then
  printf "Usage:\n %s\n"  "work_dir"
  exit
fi

work_dir="${__DIR__}/$1"

gcc \
-g \
-DHAVE_LIBCAT=1 \
-DCAT_MAGIC_BACKLOG=8192 \
-DCAT_MAGIC_PORT=9764 \
-I "${__PROJECT__}/include" \
-I "${__PROJECT__}/include/cat" \
-I "${__PROJECT__}/deps/libuv/include" \
-I "${__PROJECT__}/deps/llhttp/include" \
-I "${work_dir}" \
-c "${work_dir}/main.c" \
-o "${work_dir}/main.o"

gcc -g -o "${work_dir}/main" "${work_dir}/main.o" "${__PROJECT__}/build/libcat.a" -ldl -pthread

ulimit -n 65536
"${work_dir}/main"
